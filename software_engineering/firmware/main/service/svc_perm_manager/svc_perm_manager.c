/**
 * @file    svc_perm_manager.c
 * @brief   权限管理服务 — 识别决策 + 人脸注册 + UI 命令处理
 *
 * @details worker 单线程处理三种输入（非阻塞轮询）：
 *          1. g_q_ui_to_perm（UI 命令：注册/删除/改密/查日志）
 *          2. g_q_feature_to_perm（特征向量：1:N 识别）
 *          3. evt_bus 云端下行指令（保留）
 *
 * @author  xiamu
 * @version 2.0
 */

#include "svc_perm_manager.h"
#include "svc_camera.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "msg_queues.h"
#include "face_engine.h"
#include "db_store.h"
#include "log_sink.h"
#include "mw_event_bus.h"
#include "service_event.h"

#define SVC_PERM_TAG           "SVC_PERM"
#define SVC_PERM_Q_DEPTH       8
#define RECOG_THRESH_Q10       (CONFIG_FACE_RECOG_THRESH)

/* 人员特征缓存（PSRAM，1:N 比对用） */
#define PERM_DB_CACHE_MAX  64
static db_person_t *s_db_cache = NULL;
static uint16_t     s_db_count = 0;
static bool         s_db_dirty = true;

static const dal_relay_ops_t *s_lock_ops = NULL, *s_alarm_ops = NULL;
static void                  *s_lock_ctx = NULL, *s_alarm_ctx = NULL;
static QueueHandle_t  s_cmd_q = NULL;
static TaskHandle_t   s_worker = NULL;
static volatile bool  s_cancel_enroll = false; /**< Back 按钮取消注册 */
static volatile bool  s_enrolling     = false; /**< 注册进行中，禁止识别开门 */
static bool           s_inited = false;

/* ================================================================
 *  辅助
 * ================================================================ */

static void db_cache_refresh(void)
{
    if (s_db_cache == NULL) {
        s_db_cache = (db_person_t *)heap_caps_malloc(
            PERM_DB_CACHE_MAX * sizeof(db_person_t), MALLOC_CAP_SPIRAM);
        if (s_db_cache == NULL) { ESP_LOGE(SVC_PERM_TAG, "db cache alloc failed"); return; }
    }
    s_db_count = db_person_list(s_db_cache, PERM_DB_CACHE_MAX);
    s_db_dirty = false;
    ESP_LOGI(SVC_PERM_TAG, "db cache: %u persons", (unsigned)s_db_count);
}

static void send_ui_result(perm_cmdtype_t cmd, dal_err_t result,
                           uint32_t user_id, const char *msg)
{
    perm_result_t r = { .cmd = cmd, .result = result, .user_id = user_id };
    snprintf(r.msg, sizeof(r.msg), "%s", msg ? msg : "");
    xQueueSend(g_q_perm_to_ui, &r, 0);
}

static void door_lock_pulse(uint32_t pulse_ms)
{
    if (s_lock_ops == NULL || s_lock_ops->set == NULL) return;
    s_lock_ops->set(s_lock_ctx, true);
    uint32_t remain = pulse_ms;
    while (remain > 0) { uint32_t d = remain > 100 ? 100 : remain; vTaskDelay(pdMS_TO_TICKS(d)); remain -= d; }
    s_lock_ops->set(s_lock_ctx, false);
}

/* ================================================================
 *  1:N 识别
 * ================================================================ */

static void handle_identify(const feature_to_perm_t *ftp)
{
    /* 注册期间跳过识别，避免注册流程与识别流水线冲突 */
    if (s_enrolling) {
        heap_caps_free(ftp->features);
        return;
    }
    if (s_db_dirty) db_cache_refresh();
    if (s_db_count == 0 || s_db_cache == NULL) { heap_caps_free(ftp->features); return; }

    /* face_feature_t 含 float[1024]=4KB，放栈溢出 → PSRAM */
    face_feature_t *query = (face_feature_t *)heap_caps_malloc(
        sizeof(face_feature_t), MALLOC_CAP_SPIRAM);
    if (query == NULL) { heap_caps_free(ftp->features); return; }
    memset(query, 0, sizeof(face_feature_t));
    query->dim = ftp->dim;
    if (ftp->dim > FACE_FEATURE_DIM) query->dim = FACE_FEATURE_DIM;
    memcpy(query->vec, ftp->features, query->dim * sizeof(float));

    float best_score = 0.0f;
    uint32_t best_id = 0;
    for (uint16_t i = 0; i < s_db_count; i++) {
        float score = 0.0f;
        if (face_engine_compare(query, &s_db_cache[i].feature, &score) == DAL_OK) {
            if (score > best_score) { best_score = score; best_id = s_db_cache[i].id; }
        }
    }

    float thresh = (float)RECOG_THRESH_Q10 / 1000.0f;
    if (best_score >= thresh) {
        ESP_LOGI(SVC_PERM_TAG, "PASS id=%lu score=%.3f", (unsigned long)best_id, best_score);

        /* 先发 UI 弹框通知，再开门——避免 door_lock_pulse 阻塞导致弹框延迟 */
        char disp_name[32] = "";
        for (uint16_t i = 0; i < s_db_count; i++) {
            if (s_db_cache[i].id == best_id) {
                snprintf(disp_name, sizeof(disp_name), "%s", s_db_cache[i].name);
                break;
            }
        }
        perm_result_t pr = { .cmd = PERM_CMD_RECOG_RESULT, .result = DAL_OK,
                             .user_id = best_id };
        snprintf(pr.user_name, sizeof(pr.user_name), "%s", disp_name);
        snprintf(pr.msg, sizeof(pr.msg), "PASS");
        xQueueSend(g_q_perm_to_ui, &pr, 0);

        door_lock_pulse((uint32_t)CONFIG_FACE_LOCK_PULSE_MS);
        db_record_t rec = { .person_id = best_id, .result = 0, .method = 0 };
        db_record_add(&rec);
        log_sink_push(LOG_LVL_INFO, SVC_PERM_TAG, "PASS id=%lu score=%.3f", (unsigned long)best_id, best_score);

        /* 排空特征队列 */
        feature_to_perm_t drop;
        while (xQueueReceive(g_q_feature_to_perm, &drop, 0) == pdTRUE)
            heap_caps_free(drop.features);
    } else {
        ESP_LOGI(SVC_PERM_TAG, "REJECT best=%.3f", best_score);
        db_record_t rec = { .person_id = 0, .result = 1, .method = 0 };
        db_record_add(&rec);
        log_sink_push(LOG_LVL_INFO, SVC_PERM_TAG, "REJECT score=%.3f", best_score);

        perm_result_t pr = { .cmd = PERM_CMD_RECOG_RESULT, .result = DAL_ERR_BUSY,
                             .user_id = 0 };
        snprintf(pr.msg, sizeof(pr.msg), "REJECT");
        xQueueSend(g_q_perm_to_ui, &pr, 0);
    }
    heap_caps_free(ftp->features);
    heap_caps_free(query);
}

/* ================================================================
 *  人脸注册 — 固定区域裁剪 + 多帧均值特征
 *
 *  流程：camera RESUME → 采集 3~4s 内多帧 → 从固定区域 (x=60,y=90,
 *  280×280, 对应 UI 圆形框) 裁剪 112×112 → 逐帧提取特征 → 累加均值
 *  → db_person_add 入库 → PAUSE 停流
 *
 *  无需人脸检测：圆形框已固定注册区域，直接裁剪该区域即可。
 * ================================================================ */

#define ENROLL_FACE_X     60    /**< 人脸区域 X（camera 帧坐标） */
#define ENROLL_FACE_Y     90    /**< 人脸区域 Y */
#define ENROLL_FACE_W     280   /**< 人脸区域宽 */
#define ENROLL_FACE_H     280   /**< 人脸区域高 */
#define ENROLL_CROP_SZ     112   /**< 裁剪目标尺寸 */
#define ENROLL_DEADLINE_MS 4000  /**< 注册总时长上限 */
#define ENROLL_MIN_FRAMES   5    /**< 最少有效帧数 */
#define ENROLL_INTERVAL_MS  150  /**< 帧间隔 (~6.7 fps) */

static void handle_enroll(const ui_to_perm_t *ui_cmd)
{
    s_cancel_enroll = false;
    s_enrolling     = true;   /* 注册期间禁止识别 */

    /* 排空 feature 队列——注册前的残留特征不应触发识别 */
    feature_to_perm_t drop;
    while (xQueueReceive(g_q_feature_to_perm, &drop, 0) == pdTRUE) {
        heap_caps_free(drop.features);
    }

    camera_cmdmsg_t cam_cmd = { .cmd = CAM_CMD_RESUME };
    xQueueSend(g_q_ui_to_cam, &cam_cmd, 0);

    /* PSRAM 分配特征缓冲 */
    face_feature_t *sum_feat = (face_feature_t *)heap_caps_malloc(
        sizeof(face_feature_t), MALLOC_CAP_SPIRAM);
    face_feature_t *cur_feat = (face_feature_t *)heap_caps_malloc(
        sizeof(face_feature_t), MALLOC_CAP_SPIRAM);
    uint8_t *crop_buf = (uint8_t *)heap_caps_malloc(
        ENROLL_CROP_SZ * ENROLL_CROP_SZ * 3, MALLOC_CAP_SPIRAM);

    if (!sum_feat || !cur_feat || !crop_buf) {
        send_ui_result(PERM_CMD_ADD_USER, DAL_ERR_NO_MEM, 0, "No memory");
        goto done;
    }
    memset(sum_feat, 0, sizeof(face_feature_t));

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(ENROLL_DEADLINE_MS);
    int frame_count = 0;
    char msg[64];

    send_ui_result(PERM_CMD_ADD_USER, DAL_ERR_BUSY, 0, "Collecting...");

    /* ---- 采集循环 ---- */
    while (xTaskGetTickCount() < deadline && !s_cancel_enroll) {
        svc_frame_t fb;
        uint32_t seq = 0;
        if (!svc_camera_acquire_frame(&fb, &seq)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* 固定区域裁剪 280×280 → 112×112（最近邻） */
        const uint8_t *src = (const uint8_t *)fb.buf;
        for (int dy = 0; dy < ENROLL_CROP_SZ; dy++) {
            int sy = ENROLL_FACE_Y + (dy * ENROLL_FACE_H / ENROLL_CROP_SZ);
            const uint8_t *sr = src + ((size_t)sy * fb.width + ENROLL_FACE_X) * 3;
            uint8_t *dd = crop_buf + dy * ENROLL_CROP_SZ * 3;
            for (int dx = 0; dx < ENROLL_CROP_SZ; dx++) {
                int sx = (dx * ENROLL_FACE_W / ENROLL_CROP_SZ);
                dd[0] = sr[sx * 3];
                dd[1] = sr[sx * 3 + 1];
                dd[2] = sr[sx * 3 + 2];
            }
        }
        svc_camera_release_frame(&fb);

        /* 特征提取 */
        face_image_t img = { .data = crop_buf, .width = ENROLL_CROP_SZ,
                             .height = ENROLL_CROP_SZ, .format = 1 };
        dal_err_t ret = face_engine_extract(&img, cur_feat);

        if (ret == DAL_OK && cur_feat->dim > 0) {
            /* 在线累加均值 */
            if (frame_count == 0) {
                memcpy(sum_feat, cur_feat, sizeof(face_feature_t));
            } else {
                for (uint32_t i = 0; i < cur_feat->dim; i++) {
                    sum_feat->vec[i] = (sum_feat->vec[i] * frame_count
                                        + cur_feat->vec[i]) / (frame_count + 1);
                }
            }
            frame_count++;
            snprintf(msg, sizeof(msg), "%d frames...", frame_count);
            send_ui_result(PERM_CMD_ADD_USER, DAL_ERR_BUSY, 0, msg);
        } else {
            snprintf(msg, sizeof(msg), "%d frames (skip)...", frame_count);
            send_ui_result(PERM_CMD_ADD_USER, DAL_ERR_BUSY, 0, msg);
        }

        vTaskDelay(pdMS_TO_TICKS(ENROLL_INTERVAL_MS));
    }

    /* ---- 完成/取消 ---- */
    if (s_cancel_enroll) {
        s_cancel_enroll = false;
        send_ui_result(PERM_CMD_ADD_USER, DAL_ERR_BUSY, 0, "Cancelled");
        goto done;
    }

    if (frame_count < ENROLL_MIN_FRAMES) {
        send_ui_result(PERM_CMD_ADD_USER, DAL_ERR_BUSY, 0, "Face not found");
        goto done;
    }

    /* 入库 */
    uint32_t new_id = (uint32_t)db_person_count() + 1;
    {   db_person_t probe;
        while (db_person_get(new_id, &probe) == DAL_OK) new_id++; }

    db_person_t p;
    memset(&p, 0, sizeof(p));
    p.id = new_id;
    p.permission = 1;
    snprintf(p.name, DB_PERSON_NAME_LEN, "%s", ui_cmd->user_name);
    sum_feat->dim = cur_feat->dim;
    memcpy(&p.feature, sum_feat, sizeof(face_feature_t));

    dal_err_t r = db_person_add(&p);
    s_db_dirty = true;

    if (r == DAL_OK) {
        snprintf(msg, sizeof(msg), "Enrolled ID=%lu", (unsigned long)new_id);
        send_ui_result(PERM_CMD_ADD_USER, DAL_OK, new_id, msg);
    } else {
        send_ui_result(PERM_CMD_ADD_USER, DAL_ERR_HW, 0, "DB write failed");
    }

done:
    /* 排空注册期间积压的特征 */
    feature_to_perm_t drop2;
    while (xQueueReceive(g_q_feature_to_perm, &drop2, 0) == pdTRUE) {
        heap_caps_free(drop2.features);
    }
    /* s_enrolling 不在此清零——留到 switch_screen(ADMIN) 时清。
     * camera 不在此暂停——PAUSE 会导致 ISP 停流瞬间显示闪屏。 */
    if (sum_feat) heap_caps_free(sum_feat);
    if (cur_feat) heap_caps_free(cur_feat);
    if (crop_buf) heap_caps_free(crop_buf);
}

/* ================================================================
 *  UI 命令
 * ================================================================ */

static void handle_ui_cmd(const ui_to_perm_t *cmd)
{
    switch (cmd->cmd) {
    case PERM_CMD_ADD_USER:
        handle_enroll(cmd);
        break;
    case PERM_CMD_DELETE_USER: {
        dal_err_t r = db_person_del(cmd->user_id);
        s_db_dirty = true;
        send_ui_result(PERM_CMD_DELETE_USER, r, cmd->user_id,
                       r == DAL_OK ? "已删除" : "删除失败");
        break;
    }
    case PERM_CMD_MODIFY_USER: {
        dal_err_t r = db_config_set_str("admin.pwd", cmd->user_name);
        send_ui_result(PERM_CMD_MODIFY_USER, r, 0,
                       r == DAL_OK ? "密码已更改" : "更改失败");
        break;
    }
    case PERM_CMD_GET_LIST:
        send_ui_result(PERM_CMD_GET_LIST, DAL_OK, 0, "list ready");
        break;
    default:
        break;
    }
}

/* ================================================================
 *  云端下行（保留）
 * ================================================================ */

static void exec_remote_open(const service_downlink_cmd_t *cmd)
{
    (void)cmd;
    door_lock_pulse((uint32_t)CONFIG_FACE_LOCK_PULSE_MS);
    db_record_t rec = { .person_id = 0, .result = 0, .method = 1 };
    db_record_add(&rec);
}

static void on_cmd_downlink(const service_event_t *event, void *user_data)
{
    (void)user_data;
    if (event == NULL || event->data == NULL || s_cmd_q == NULL) return;
    xQueueSend(s_cmd_q, event->data, 0);
}

/* ================================================================
 *  worker
 * ================================================================ */

static void perm_worker(void *arg)
{
    (void)arg;
    ESP_LOGI(SVC_PERM_TAG, "worker started");

    for (;;) {
        ui_to_perm_t ui_cmd;
        if (xQueueReceive(g_q_ui_to_perm, &ui_cmd, 0) == pdTRUE) {
            handle_ui_cmd(&ui_cmd);
            continue;
        }

        service_downlink_cmd_t dl_cmd;
        if (s_cmd_q && xQueueReceive(s_cmd_q, &dl_cmd, 0) == pdTRUE) {
            if (dl_cmd.type == SERVICE_CMD_REMOTE_OPEN) exec_remote_open(&dl_cmd);
            continue;
        }

        feature_to_perm_t ftp;
        if (xQueueReceive(g_q_feature_to_perm, &ftp, 0) == pdTRUE) {
            handle_identify(&ftp);
            continue;
        }

        if (s_db_dirty) db_cache_refresh();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ================================================================
 *  公共 API
 * ================================================================ */

void svc_perm_manager_cancel_enroll(void)
{
    s_cancel_enroll = true;
}

void svc_perm_manager_set_enrolling(bool en)
{
    s_enrolling = en;
}

bool svc_perm_manager_is_enrolling(void)
{
    return s_enrolling;
}

dal_err_t svc_perm_manager_init(const svc_perm_manager_deps_t *deps)
{
    if (s_inited) return DAL_ERR_STATE;
    if (deps == NULL || deps->door_lock_ops == NULL) return DAL_ERR_INVALID;

    s_lock_ops = deps->door_lock_ops; s_lock_ctx = deps->door_lock_ctx;
    s_alarm_ops = deps->alarm_ops;     s_alarm_ctx = deps->alarm_ctx;
    if (s_lock_ops->init) s_lock_ops->init(s_lock_ctx);
    if (s_alarm_ops && s_alarm_ops->init) s_alarm_ops->init(s_alarm_ctx);

    s_cmd_q = xQueueCreate(SVC_PERM_Q_DEPTH, sizeof(service_downlink_cmd_t));
    if (s_cmd_q == NULL) return DAL_ERR_NO_MEM;
    mw_event_bus_subscribe(SERVICE_EVT_CMD_DOWNLINK, on_cmd_downlink, NULL);

    if (xTaskCreatePinnedToCore(perm_worker, "svc_perm",
        CONFIG_FACE_PERM_TASK_STACK, NULL,
        CONFIG_FACE_PERM_TASK_PRIO, &s_worker, 0) != pdPASS) {
        vQueueDelete(s_cmd_q); s_cmd_q = NULL;
        return DAL_ERR_NO_MEM;
    }

    s_inited = true;
    ESP_LOGI(SVC_PERM_TAG, "init ok");
    return DAL_OK;
}
