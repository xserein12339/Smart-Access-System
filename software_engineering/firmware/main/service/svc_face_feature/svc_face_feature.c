#include "svc_face_feature.h"
#include "svc_perm_manager.h"
#include "msg_queues.h"
#include "face_engine.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "SVC_FEAT"

static TaskHandle_t s_worker = NULL;
static bool        s_inited = false;

static void s_feature_task(void *arg)
{
    (void)arg;
    while (!face_engine_is_ready()) vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "worker started");
    uint32_t count = 0;

    for (;;) {
        detect_to_feature_t dtf;
        if (xQueueReceive(g_q_det_to_feature, &dtf, pdMS_TO_TICKS(1000)) != pdTRUE)
            continue;

        /* 注册期间跳过特征提取——注册用的是 handle_enroll 固定裁剪，
         * 不需要 detect → feature → perm 这条流水线 */
        if (svc_perm_manager_is_enrolling()) {
            if (dtf.buffer) heap_caps_free(dtf.buffer);
            continue;
        }

        face_image_t img = {
            .data   = dtf.buffer, .width  = dtf.width,
            .height = dtf.height, .format = 1,
        };
        face_feature_t feat;
        dal_err_t ret = face_engine_extract(&img, &feat);
        if (ret == DAL_OK && feat.dim > 0) {
            float *f = heap_caps_malloc(feat.dim * sizeof(float), MALLOC_CAP_SPIRAM);
            if (f) {
                memcpy(f, feat.vec, feat.dim * sizeof(float));
                feature_to_perm_t ftp = { .features = f, .face_hash = dtf.face_hash, .dim = feat.dim };
                if (xQueueSend(g_q_feature_to_perm, &ftp, 0) == pdTRUE) {
                    count++;
                    ESP_LOGI(TAG, "feat ok: dim=%u hash=%lu", (unsigned)feat.dim, (unsigned long)dtf.face_hash);
                } else {
                    heap_caps_free(f);
                    ESP_LOGW(TAG, "feat queue full, drop");
                }
            } else {
                ESP_LOGE(TAG, "feat malloc failed");
            }
        } else {
            ESP_LOGW(TAG, "extract failed: ret=%d dim=%u", (int)ret, (unsigned)feat.dim);
        }
        if (dtf.buffer) heap_caps_free(dtf.buffer);
        if ((count & 0x3F) == 0)
            ESP_LOGI(TAG, "extracted %lu features total", (unsigned long)count);
    }
}

dal_err_t svc_face_feature_init(void)
{
    if (s_inited) return DAL_ERR_STATE;
    s_inited = true; return DAL_OK;
}

dal_err_t svc_face_feature_start(void)
{
    if (!s_inited) return DAL_ERR_STATE;
    if (s_worker) return DAL_OK;
    if (xTaskCreatePinnedToCore(s_feature_task, "svc_feat",
        CONFIG_FACE_FACE_TASK_STACK, NULL,
        CONFIG_FACE_FACE_TASK_PRIO, &s_worker, 0) != pdPASS)
        return DAL_ERR_NO_MEM;
    ESP_LOGI(TAG, "task created"); return DAL_OK;
}

dal_err_t svc_face_feature_stop(void)
{
    if (!s_worker) return DAL_OK;
    vTaskDelete(s_worker); s_worker = NULL; return DAL_OK;
}
