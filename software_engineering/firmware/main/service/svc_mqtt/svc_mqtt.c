/**
 * @file    svc_mqtt.c
 * @brief   MQTT 通信服务实现 — 纯管道 + OTA 路由
 *
 * @details 下行：MQTT_DATA → 按 subtopic 后缀定 op → cJSON 解析 →
 *          remote_open/user_add/user_del → service_downlink_cmd_t → CMD_DOWNLINK
 *          ota_start → service_ota_cmd_t → CMD_OTA（跳过 perm_manager，管理操作）
 *          上行：worker 订阅 AUTH/RECOG/FAULT/CMD_ACK/OTA_PROGRESS/OTA_RESULT →
 *          序列化 JSON → esp_mqtt_client_publish。
 *
 * @author  xiamu
 * @version 1.1
 */
#include "svc_mqtt.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"

#include "mw_event_bus.h"
#include "service_event.h"
#include "db_store.h"

#define SVC_MQTT_TAG             "SVC_MQTT"
#define SVC_MQTT_FW_VERSION      "1.0.0"
#define SVC_MQTT_TOPIC_PREFIX    "face_access/"
#define SVC_MQTT_UPLINK_Q_DEPTH  16
#define SVC_MQTT_CMD_POOL_SIZE   8

/* ================================================================
 *  上行队列项
 * ================================================================ */
typedef enum {
    UPLINK_RECORD, UPLINK_RECOG, UPLINK_FAULT, UPLINK_CMD_ACK,
    UPLINK_OTA_PROGRESS, UPLINK_OTA_RESULT,
} uplink_kind_t;

typedef struct {
    uplink_kind_t kind;
    uint32_t msg_id, person_id, timestamp;
    uint8_t  arg0, arg1;
    float    confidence, quality;
    bool     liveness;
    union { uint32_t ota_pct, ota_code; };
    union { uint32_t ota_bytes; };
    char     msg[64];
} uplink_item_t;

/* ---- 模块状态 ---- */
static const dal_network_ops_t *s_net_ops = NULL;
static void                    *s_net_ctx = NULL;
static esp_mqtt_client_handle_t s_client  = NULL;
static QueueHandle_t            s_uplink_q = NULL;
static TaskHandle_t             s_worker   = NULL;
static bool                     s_inited   = false;
static bool                     s_running  = false;
static char s_device_id[32]  = {0};
static char s_topic_base[48] = {0};
static uint32_t s_uplink_msg_id = 1;
static service_downlink_cmd_t s_cmd_pool[SVC_MQTT_CMD_POOL_SIZE];
static service_ota_cmd_t      s_ota_cmd_pool[4];
static volatile uint8_t s_cmd_pool_idx = 0;
static volatile uint8_t s_ota_pool_idx = 0;

/* ================================================================
 *  工具
 * ================================================================ */
static uint32_t uptime_s(void) { return (uint32_t)(esp_timer_get_time() / 1000000LL); }

static void topic_build(char *buf, size_t buf_len, const char *sub) {
    snprintf(buf, buf_len, "%s%s", s_topic_base, sub);
}

static void publish_uplink(const char *sub, const char *json, int qos) {
    if (s_client == NULL || json == NULL) return;
    char topic[64]; topic_build(topic, sizeof(topic), sub);
    esp_mqtt_client_publish(s_client, topic, json, (int)strlen(json), qos, 0);
}

static service_downlink_cmd_t *cmd_pool_alloc(void) {
    service_downlink_cmd_t *c = &s_cmd_pool[s_cmd_pool_idx];
    s_cmd_pool_idx = (s_cmd_pool_idx + 1u) % SVC_MQTT_CMD_POOL_SIZE;
    memset(c, 0, sizeof(*c)); return c;
}

static service_ota_cmd_t *ota_pool_alloc(void) {
    service_ota_cmd_t *c = &s_ota_cmd_pool[s_ota_pool_idx];
    s_ota_pool_idx = (s_ota_pool_idx + 1u) % 4;
    memset(c, 0, sizeof(*c)); return c;
}

/* ================================================================
 *  下行：命令路由
 * ================================================================ */
static service_cmd_type_t cmd_from_subtopic(const char *topic, size_t tlen) {
    char buf[64]; if (tlen >= sizeof(buf)) return (service_cmd_type_t)0;
    memcpy(buf, topic, tlen); buf[tlen] = '\0';
    const char *p = strstr(buf, "/cmd/"); if (p == NULL) return (service_cmd_type_t)0;
    p += 5;
    if (strcmp(p, "remote_open") == 0) return SERVICE_CMD_REMOTE_OPEN;
    if (strcmp(p, "user_add")    == 0) return SERVICE_CMD_USER_ADD;
    if (strcmp(p, "user_del")    == 0) return SERVICE_CMD_USER_DEL;
    if (strcmp(p, "ota_start")   == 0) return SERVICE_CMD_OTA_START;
    return (service_cmd_type_t)0;
}

static char *copy_body(esp_mqtt_event_handle_t evt) {
    size_t blen = evt->data_len < 255u ? (size_t)evt->data_len : 255u;
    static char buf[256];
    memcpy(buf, evt->data, blen); buf[blen] = '\0'; return buf;
}

static uint32_t json_uint(cJSON *root, const char *key) {
    cJSON *n = cJSON_GetObjectItem(root, key);
    return cJSON_IsNumber(n) ? (uint32_t)n->valuedouble : 0;
}

static void json_copy_str(cJSON *root, const char *key, char *dst, size_t size) {
    cJSON *n = cJSON_GetObjectItem(root, key);
    if (cJSON_IsString(n) && n->valuestring) strncpy(dst, n->valuestring, size - 1u);
}

/** 处理普通业务命令（→ CMD_DOWNLINK） */
static void handle_biz_cmd(service_cmd_type_t cmd, esp_mqtt_event_handle_t evt) {
    char *body = copy_body(evt);
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) return;
    service_downlink_cmd_t *p = cmd_pool_alloc();
    p->type = cmd;
    p->msg_id = json_uint(root, "msg_id");
    p->person_id = json_uint(root, "person_id");
    json_copy_str(root, "name", p->name, sizeof(p->name));
    /* 完整 JSON 正文保存供 svc_perm_manager 解析（user_add/user_del 额外字段） */
    strncpy(p->body, body, sizeof(p->body) - 1u);
    cJSON_Delete(root);
    ESP_LOGI(SVC_MQTT_TAG, "downlink: cmd=%d msg_id=%lu", (int)p->type, (unsigned long)p->msg_id);
    service_event_t e = {.type=SERVICE_EVT_CMD_DOWNLINK, .source=SERVICE_SRC_MQTT, .data=p};
    mw_event_bus_publish(&e);
}

/** 处理 OTA 命令（→ CMD_OTA，跳过 perm_manager） */
static void handle_ota_cmd(esp_mqtt_event_handle_t evt) {
    char *body = copy_body(evt);
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) return;
    service_ota_cmd_t *p = ota_pool_alloc();
    json_copy_str(root, "version", p->version, sizeof(p->version));
    p->size    = json_uint(root, "size");
    json_copy_str(root, "sha256", p->sha256, sizeof(p->sha256));
    json_copy_str(root, "url",    p->url,    sizeof(p->url));
    p->msg_id  = json_uint(root, "msg_id");
    cJSON_Delete(root);
    ESP_LOGI(SVC_MQTT_TAG, "downlink: ota ver=%s size=%lu msg_id=%lu",
             p->version, (unsigned long)p->size, (unsigned long)p->msg_id);
    service_event_t e = {.type=SERVICE_EVT_CMD_OTA, .source=SERVICE_SRC_MQTT, .data=p};
    mw_event_bus_publish(&e);
}

/* ================================================================
 *  MQTT 事件回调
 * ================================================================ */
static void mqtt_event_handler(void *args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
    (void)args; (void)base;
    esp_mqtt_event_handle_t evt = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        ESP_LOGI(SVC_MQTT_TAG, "connected to broker");
        char sub_topic[64]; topic_build(sub_topic, sizeof(sub_topic), "cmd/#");
        esp_mqtt_client_subscribe(s_client, sub_topic, 1);
        service_event_t e = {.type=SERVICE_EVT_MQTT_CONNECTED, .source=SERVICE_SRC_MQTT};
        mw_event_bus_publish(&e);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(SVC_MQTT_TAG, "disconnected");
        { service_event_t e = {.type=SERVICE_EVT_MQTT_DISCONNECTED, .source=SERVICE_SRC_MQTT};
          mw_event_bus_publish(&e); }
        break;
    case MQTT_EVENT_DATA: {
        service_cmd_type_t ct = cmd_from_subtopic(evt->topic, (size_t)evt->topic_len);
        if (ct == SERVICE_CMD_OTA_START) handle_ota_cmd(evt);
        else if (ct != (service_cmd_type_t)0) handle_biz_cmd(ct, evt);
        else ESP_LOGW(SVC_MQTT_TAG, "unknown cmd topic");
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGE(SVC_MQTT_TAG, "mqtt error type=%d", evt->error_handle ? (int)evt->error_handle->error_type : -1);
        break;
    default: break;
    }
}

/* ================================================================
 *  上行：业务事件回调 → 入队
 * ================================================================ */
static void on_uplink(const service_event_t *event, void *user_data) {
    (void)user_data;
    if (event == NULL || s_uplink_q == NULL) return;
    uplink_item_t item = {0};
    switch (event->type) {
    case SERVICE_EVT_AUTH_RESULT: {
        const service_auth_ctx_t *ctx = (const service_auth_ctx_t *)event->data;
        item.kind=UPLINK_RECORD; item.arg0=(uint8_t)event->arg0; item.arg1=(uint8_t)event->arg1;
        item.person_id=ctx?ctx->person_id:0; item.timestamp=ctx?ctx->timestamp:0;
        item.msg_id=ctx?ctx->msg_id:0; break;
    }
    case SERVICE_EVT_RECOG_RESULT: {
        const service_recog_result_t *r=(const service_recog_result_t*)event->data;
        item.kind=UPLINK_RECOG; item.person_id=r?r->person_id:0;
        item.confidence=r?r->confidence:0; item.quality=r?r->quality:0;
        item.liveness=r?r->liveness:false; break;
    }
    case SERVICE_EVT_FAULT:
        item.kind=UPLINK_FAULT; item.arg0=(uint8_t)event->arg0; break;
    case SERVICE_EVT_CMD_ACK:
        item.kind=UPLINK_CMD_ACK; item.msg_id=event->arg0; item.arg0=(uint8_t)event->arg1; break;
    case SERVICE_EVT_OTA_PROGRESS:
        item.kind=UPLINK_OTA_PROGRESS; item.ota_pct=event->arg0; item.ota_bytes=event->arg1; break;
    case SERVICE_EVT_OTA_RESULT: {
        const service_ota_result_t *r=(const service_ota_result_t*)event->data;
        item.kind=UPLINK_OTA_RESULT; item.msg_id=r?r->msg_id:0; item.ota_code=r?r->code:0;
        if (r) { strncpy(item.msg, r->msg, sizeof(item.msg)-1u); } break;
    }
    default: return;
    }
    if (xQueueSend(s_uplink_q, &item, 0) != pdTRUE)
        ESP_LOGW(SVC_MQTT_TAG, "uplink queue full, dropped kind=%d", (int)item.kind);
}

/* ================================================================
 *  上行：worker 序列化 + 心跳
 * ================================================================ */
static void worker_publish_record(const uplink_item_t *it) {
    char j[160]; snprintf(j,sizeof(j),
      "{\"msg_id\":%lu,\"person_id\":%lu,\"timestamp\":%lu,\"result\":%u,\"method\":%u}",
      (unsigned long)it->msg_id,(unsigned long)it->person_id,(unsigned long)it->timestamp,it->arg0,it->arg1);
    publish_uplink("evt/record", j, 1);
}
static void worker_publish_recog(const uplink_item_t *it) {
    char j[200]; snprintf(j,sizeof(j),
      "{\"msg_id\":%lu,\"person_id\":%lu,\"confidence\":%d,\"quality\":%d,\"liveness\":%s,\"timestamp\":%lu}",
      (unsigned long)it->msg_id,(unsigned long)it->person_id,(int)(it->confidence*1000),
      (int)(it->quality*1000),it->liveness?"true":"false",(unsigned long)it->timestamp);
    publish_uplink("evt/recog", j, 1);
}
static void worker_publish_fault(const uplink_item_t *it) {
    char j[96]; snprintf(j,sizeof(j),
      "{\"msg_id\":%lu,\"code\":%u,\"msg\":\"fault_%u\",\"timestamp\":0}",
      (unsigned long)s_uplink_msg_id++,it->arg0,it->arg0);
    publish_uplink("evt/fault", j, 1);
}
static void worker_publish_cmd_ack(const uplink_item_t *it) {
    static const char *k[]={"ok","rejected","db_error","unknown","dup"};
    char j[96]; snprintf(j,sizeof(j),
      "{\"msg_id\":%lu,\"code\":%u,\"msg\":\"%s\"}",
      (unsigned long)it->msg_id,it->arg0,it->arg0<5?k[it->arg0]:"err");
    publish_uplink("evt/cmd_ack", j, 1);
}
static void worker_publish_ota_progress(const uplink_item_t *it) {
    char j[128]; snprintf(j,sizeof(j),
      "{\"msg_id\":%lu,\"percent\":%lu,\"downloaded\":%lu,\"total\":0}",
      (unsigned long)s_uplink_msg_id++,(unsigned long)it->ota_pct,(unsigned long)it->ota_bytes);
    publish_uplink("evt/ota_progress", j, 1);
}
static void worker_publish_ota_result(const uplink_item_t *it) {
    static const char *k[]={"ok","download_failed","sha256_mismatch","write_error","invalid_url"};
    char j[160]; snprintf(j,sizeof(j),
      "{\"msg_id\":%lu,\"code\":%lu,\"msg\":\"%s\"}",
      (unsigned long)it->msg_id,(unsigned long)it->ota_code,
      it->ota_code<5?k[it->ota_code]:"err");
    publish_uplink("evt/ota_result", j, 1);
}
static void worker_publish_online(void) {
    char ip[16]={0}; if(s_net_ops&&s_net_ops->get_ip) s_net_ops->get_ip(s_net_ctx,ip,sizeof(ip));
    char j[192]; snprintf(j,sizeof(j),
      "{\"device_id\":\"%s\",\"online\":true,\"ip\":\"%s\",\"uptime_s\":%lu,\"fw\":\"%s\",\"db_count\":%u}",
      s_device_id,ip,(unsigned long)uptime_s(),SVC_MQTT_FW_VERSION,(unsigned)db_person_count());
    publish_uplink("evt/online", j, 0);
}

static void uplink_worker(void *arg) {
    (void)arg; esp_task_wdt_add(NULL);
    uint32_t last_hb=0; uplink_item_t it;
    for(;;){
        if(xQueueReceive(s_uplink_q,&it,pdMS_TO_TICKS(1000))==pdTRUE){
            switch(it.kind){
            case UPLINK_RECORD: worker_publish_record(&it); break;
            case UPLINK_RECOG:  worker_publish_recog(&it);  break;
            case UPLINK_FAULT:  worker_publish_fault(&it);  break;
            case UPLINK_CMD_ACK:worker_publish_cmd_ack(&it);break;
            case UPLINK_OTA_PROGRESS: worker_publish_ota_progress(&it); break;
            case UPLINK_OTA_RESULT:   worker_publish_ota_result(&it);   break;
            }
        }
        uint32_t now=uptime_s();
        if(s_running&&(now-last_hb)>=(uint32_t)CONFIG_FACE_HB_INTERVAL_S){last_hb=now;worker_publish_online();}
        esp_task_wdt_reset();
    }
}

/* ================================================================
 *  配置解析 + 公共 API
 * ================================================================ */
static void load_string(const char *nvs_key, const char *kconfig_def, char *out, size_t len) {
    db_config_get_str(nvs_key, out, (uint16_t)len, kconfig_def);
}

dal_err_t svc_mqtt_init(const svc_mqtt_deps_t *deps) {
    if(s_inited) return DAL_ERR_STATE;
    if(deps==NULL) return DAL_ERR_INVALID;
    s_net_ops=deps->network_ops; s_net_ctx=deps->network_ctx;

    load_string("dev.device_id", CONFIG_FACE_DEVICE_ID, s_device_id, sizeof(s_device_id));
    if(s_device_id[0]=='\0') strncpy(s_device_id,"FACE-UNKNOWN",sizeof(s_device_id)-1u);
    snprintf(s_topic_base,sizeof(s_topic_base),"%s%s/",SVC_MQTT_TOPIC_PREFIX,s_device_id);

    char broker[128]={0}, user[64]={0}, pass[64]={0};
    load_string("mqtt.broker", CONFIG_FACE_MQTT_BROKER_URI,broker,sizeof(broker));
    load_string("mqtt.user",   CONFIG_FACE_MQTT_USERNAME,  user, sizeof(user));
    load_string("mqtt.pass",   CONFIG_FACE_MQTT_PASSWORD,  pass, sizeof(pass));
    ESP_LOGI(SVC_MQTT_TAG,"init: device=%s broker=%s",s_device_id,broker);

    char lwt_topic[64], lwt_msg[64];
    topic_build(lwt_topic,sizeof(lwt_topic),"evt/offline");
    snprintf(lwt_msg,sizeof(lwt_msg),"{\"device_id\":\"%s\",\"online\":false}",s_device_id);

    esp_mqtt_client_config_t cfg={
        .broker.address.uri=broker,
        .credentials.client_id=s_device_id,
        .credentials.username=user,
        .credentials.authentication.password=pass,
        .network.reconnect_timeout_ms=5000,
        .buffer.size=1024,.buffer.out_size=1024,
        .task.stack_size=8192,.task.priority=CONFIG_FACE_MQTT_TASK_PRIO,
        .session.last_will.topic=lwt_topic,
        .session.last_will.msg=lwt_msg,
        .session.last_will.msg_len=(int)strlen(lwt_msg),
        .session.last_will.qos=1,.session.last_will.retain=false,
        .broker.verification.crt_bundle_attach=esp_crt_bundle_attach,
    };
    s_client=esp_mqtt_client_init(&cfg);
    if(s_client==NULL){ESP_LOGE(SVC_MQTT_TAG,"client init failed");return DAL_ERR_NO_MEM;}
    esp_mqtt_client_register_event(s_client,ESP_EVENT_ANY_ID,mqtt_event_handler,NULL);

    s_uplink_q=xQueueCreate(SVC_MQTT_UPLINK_Q_DEPTH,sizeof(uplink_item_t));
    if(s_uplink_q==NULL){esp_mqtt_client_destroy(s_client);s_client=NULL;return DAL_ERR_NO_MEM;}

    mw_event_bus_subscribe(SERVICE_EVT_AUTH_RESULT,   on_uplink,NULL);
    mw_event_bus_subscribe(SERVICE_EVT_RECOG_RESULT,  on_uplink,NULL);
    mw_event_bus_subscribe(SERVICE_EVT_FAULT,         on_uplink,NULL);
    mw_event_bus_subscribe(SERVICE_EVT_CMD_ACK,       on_uplink,NULL);
    mw_event_bus_subscribe(SERVICE_EVT_OTA_PROGRESS,  on_uplink,NULL);
    mw_event_bus_subscribe(SERVICE_EVT_OTA_RESULT,    on_uplink,NULL);

    s_inited=true; ESP_LOGI(SVC_MQTT_TAG,"init ok"); return DAL_OK;
}

dal_err_t svc_mqtt_start(void) {
    if(!s_inited||s_running) return DAL_ERR_STATE;
    s_running=true;
    xTaskCreatePinnedToCore(uplink_worker,"svc_mqtt",CONFIG_FACE_MQTT_TASK_STACK,
                            NULL,CONFIG_FACE_MQTT_TASK_PRIO,&s_worker,1);  /* core 1：非核心，与 AI 管线分核 */
    if(s_worker==NULL){s_running=false;return DAL_ERR_NO_MEM;}
    esp_mqtt_client_start(s_client);
    ESP_LOGI(SVC_MQTT_TAG,"started"); return DAL_OK;
}

dal_err_t svc_mqtt_stop(void) {
    if(!s_inited) return DAL_ERR_STATE;
    s_running=false;
    if(s_client) esp_mqtt_client_stop(s_client);
    if(s_worker){vTaskDelete(s_worker);s_worker=NULL;}
    return DAL_OK;
}
