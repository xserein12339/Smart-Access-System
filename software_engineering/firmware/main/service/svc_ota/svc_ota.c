/**
 * @file    svc_ota.c
 * @brief   OTA 固件升级服务实现
 *
 * @details 数据流：svc_mqtt 解析 ota_start JSON → 发 SERVICE_EVT_CMD_OTA 事件 →
 *          本服务 evt_bus 回调（仅拷贝入队）→ worker 串行消费：
 *          HTTP GET 下载 → 流式 SHA256 → esp_ota_write → 校验 → 切换槽位 → 重启。
 *
 *          进度：每 5% 百分比变化发 SERVICE_EVT_OTA_PROGRESS (arg0=percent, arg1=bytes)。
 *          结果：成功/失败发 SERVICE_EVT_OTA_RESULT (data=service_ota_result_t)。
 *
 * @author  xiamu
 * @version 1.0
 */
#include "svc_ota.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"

#include "mw_event_bus.h"
#include "service_event.h"

#define SVC_OTA_TAG       "SVC_OTA"
#define SVC_OTA_Q_DEPTH   4       /**< OTA 命令队列（单条即可，多留余量） */
#define SVC_OTA_BUF_SIZE  4096    /**< HTTP 下载缓冲 */
#define SVC_OTA_PROG_STEP 5       /**< 每 N% 上报一次进度 */

/* ---- 模块状态 ---- */
static QueueHandle_t s_cmd_q = NULL;
static TaskHandle_t  s_worker = NULL;
static bool          s_inited = false;

/* ================================================================
 *  worker：下载 + OTA 写入 + SHA256 + 重启
 * ================================================================ */

/** 将 mbedtls SHA256 输出转为 hex 字符串（32 bytes → 64 hex + NUL） */
static void sha256_to_hex(const unsigned char *hash, char *hex, size_t hex_size)
{
    if (hex_size < 65) return;
    for (int i = 0; i < 32; i++) {
        snprintf(hex + i * 2, 3, "%02x", hash[i]);
    }
    hex[64] = '\0';
}

/** 发 OTA 进度 */
static void publish_progress(uint32_t percent, uint32_t downloaded)
{
    service_event_t e = {
        .type   = SERVICE_EVT_OTA_PROGRESS,
        .source = SERVICE_SRC_MQTT,   /* OTA 原始命令来自 MQTT，来源标 MQTT */
        .arg0   = percent,
        .arg1   = downloaded,
        .data   = NULL,
    };
    mw_event_bus_publish(&e);
}

/** 发 OTA 结果（data 指向静态结果，同步分发期内有效） */
static void publish_result(uint32_t msg_id, uint8_t code, const char *msg)
{
    static service_ota_result_t s_result;  /* 同步分发期内安全 */
    s_result.msg_id = msg_id;
    s_result.code   = code;
    if (msg) {
        strncpy(s_result.msg, msg, sizeof(s_result.msg) - 1u);
    }
    service_event_t e = {
        .type   = SERVICE_EVT_OTA_RESULT,
        .source = SERVICE_SRC_MQTT,
        .arg0   = 0,
        .arg1   = 0,
        .data   = &s_result,
    };
    mw_event_bus_publish(&e);
}

/** 执行 OTA */
static void ota_execute(const service_ota_cmd_t *cmd)
{
    ESP_LOGI(SVC_OTA_TAG, "OTA start: ver=%s size=%lu url=%s",
             cmd->version, (unsigned long)cmd->size, cmd->url);

    /* ---- 1. 获取目标 OTA 分区 ---- */
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        ESP_LOGE(SVC_OTA_TAG, "no OTA partition found");
        publish_result(cmd->msg_id, 3, "no ota partition");
        return;
    }
    ESP_LOGI(SVC_OTA_TAG, "target partition: %s (addr=0x%lx, size=%lu)",
             target->label, (unsigned long)target->address,
             (unsigned long)target->size);

    if (cmd->size > target->size) {
        ESP_LOGE(SVC_OTA_TAG, "firmware too large: %lu > %lu",
                 (unsigned long)cmd->size, (unsigned long)target->size);
        publish_result(cmd->msg_id, 3, "firmware too large");
        return;
    }

    /* ---- 2. HTTP 下载准备 ---- */
    esp_http_client_config_t http_cfg = {
        .url = cmd->url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 30000,
        .buffer_size = SVC_OTA_BUF_SIZE,
        .buffer_size_tx = 512,
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t http = esp_http_client_init(&http_cfg);
    if (http == NULL) {
        ESP_LOGE(SVC_OTA_TAG, "http init failed");
        publish_result(cmd->msg_id, 4, "http init failed");
        return;
    }

    esp_err_t err = esp_http_client_open(http, 0);
    if (err != ESP_OK) {
        ESP_LOGE(SVC_OTA_TAG, "http open failed: %s", esp_err_to_name(err));
        publish_result(cmd->msg_id, 1, "http open failed");
        esp_http_client_cleanup(http);
        return;
    }

    int content_len = esp_http_client_fetch_headers(http);
    int status = esp_http_client_get_status_code(http);
    ESP_LOGI(SVC_OTA_TAG, "http status=%d content_len=%d", status, content_len);
    if (status != 200) {
        ESP_LOGE(SVC_OTA_TAG, "http status %d != 200", status);
        publish_result(cmd->msg_id, 1, "http not 200");
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
        return;
    }

    /* ---- 3. 开始 OTA 写入 + SHA256 ---- */
    esp_ota_handle_t ota_handle = 0;
    size_t image_size = (content_len > 0) ? (size_t)content_len : (size_t)cmd->size;
    err = esp_ota_begin(target, image_size, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(SVC_OTA_TAG, "ota begin failed: %s", esp_err_to_name(err));
        publish_result(cmd->msg_id, 3, "ota begin failed");
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
        return;
    }

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);  /* 0 = SHA-256 */

    uint8_t buf[SVC_OTA_BUF_SIZE];
    uint32_t total_read = 0;
    int last_pct = 0;

    while (1) {
        int len = esp_http_client_read(http, (char *)buf, sizeof(buf));
        if (len <= 0) break;   /* 0 = 完成, <0 = 错误 */

        mbedtls_sha256_update(&sha_ctx, buf, (size_t)len);
        err = esp_ota_write(ota_handle, buf, (size_t)len);
        if (err != ESP_OK) {
            ESP_LOGE(SVC_OTA_TAG, "ota write failed: %s", esp_err_to_name(err));
            mbedtls_sha256_free(&sha_ctx);
            esp_ota_abort(ota_handle);
            esp_http_client_close(http);
            esp_http_client_cleanup(http);
            publish_result(cmd->msg_id, 3, "ota write failed");
            return;
        }
        total_read += (uint32_t)len;

        /* 进度上报 */
        uint32_t pct = image_size > 0 ? (total_read * 100u / (uint32_t)image_size) : 0u;
        if (pct - last_pct >= SVC_OTA_PROG_STEP || pct == 100u) {
            last_pct = (int)pct;
            publish_progress(pct, total_read);
            ESP_LOGI(SVC_OTA_TAG, "progress: %lu%% (%lu/%zu)",
                     (unsigned long)pct, (unsigned long)total_read, image_size);
        }
        esp_task_wdt_reset();
    }
    esp_http_client_close(http);
    esp_http_client_cleanup(http);

    /* ---- 4. 完成 SHA256 计算 ---- */
    unsigned char sha_out[32] = {0};
    char sha_hex[65] = {0};
    mbedtls_sha256_finish(&sha_ctx, sha_out);
    mbedtls_sha256_free(&sha_ctx);
    sha256_to_hex(sha_out, sha_hex, sizeof(sha_hex));

    ESP_LOGI(SVC_OTA_TAG, "download done: %lu bytes, sha256=%s",
             (unsigned long)total_read, sha_hex);

    /* SHA256 校验 */
    if (cmd->sha256[0] != '\0' && strcmp(sha_hex, cmd->sha256) != 0) {
        ESP_LOGE(SVC_OTA_TAG, "sha256 mismatch: got=%s want=%s", sha_hex, cmd->sha256);
        esp_ota_abort(ota_handle);
        publish_result(cmd->msg_id, 2, "sha256 mismatch");
        return;
    }

    /* ---- 5. 完成 OTA ---- */
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(SVC_OTA_TAG, "ota end failed: %s", esp_err_to_name(err));
        publish_result(cmd->msg_id, 3, "ota end failed");
        return;
    }

    /* ---- 6. 切换启动分区 + 重启 ---- */
    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        ESP_LOGE(SVC_OTA_TAG, "set boot partition failed: %s", esp_err_to_name(err));
        publish_result(cmd->msg_id, 3, "set boot partition failed");
        return;
    }

    ESP_LOGI(SVC_OTA_TAG, "OTA OK, rebooting to %s ...", target->label);
    publish_result(cmd->msg_id, 0, "ok, rebooting");
    /* 给 svc_mqtt 一点时间把上条消息发出去 */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

/* ================================================================
 *  worker 任务
 * ================================================================ */

static void ota_worker(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    service_ota_cmd_t cmd;

    for (;;) {
        if (xQueueReceive(s_cmd_q, &cmd, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ota_execute(&cmd);
        }
        esp_task_wdt_reset();
    }
}

/* ================================================================
 *  事件总线回调（仅拷贝入队，不阻塞分发）
 * ================================================================ */

static void on_ota_cmd(const service_event_t *event, void *user_data)
{
    (void)user_data;
    if (event == NULL || event->data == NULL || s_cmd_q == NULL) {
        return;
    }
    const service_ota_cmd_t *src = (const service_ota_cmd_t *)event->data;
    if (xQueueSend(s_cmd_q, src, 0) != pdTRUE) {
        ESP_LOGW(SVC_OTA_TAG, "cmd queue full, dropped");
    }
}

/* ================================================================
 *  公共 API
 * ================================================================ */

dal_err_t svc_ota_init(const svc_ota_deps_t *deps)
{
    (void)deps;
    if (s_inited) {
        return DAL_ERR_STATE;
    }

    s_cmd_q = xQueueCreate(SVC_OTA_Q_DEPTH, sizeof(service_ota_cmd_t));
    if (s_cmd_q == NULL) {
        return DAL_ERR_NO_MEM;
    }

    mw_event_bus_subscribe(SERVICE_EVT_CMD_OTA, on_ota_cmd, NULL);

    xTaskCreatePinnedToCore(ota_worker, "svc_ota",
                            8192, NULL, 3, &s_worker, 1);  /* core 1：非核心，与 AI 管线分核 */
    if (s_worker == NULL) {
        vQueueDelete(s_cmd_q);
        s_cmd_q = NULL;
        return DAL_ERR_NO_MEM;
    }

    s_inited = true;
    ESP_LOGI(SVC_OTA_TAG, "ota service ready");
    return DAL_OK;
}
