/**
 * @file    face_engine.c
 * @brief   人脸算法引擎实现 — ops 转发 + stub
 *
 * @details 维护全局 ops+ctx，转发 Service 调用。stub 实现使流水线在无
 *          NPU/模型时结构跑通：detect 恒返回 0 人脸，extract/compare 返回
 *          UNSUPPORTED。后续替换为真实 ops 即可。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "face_engine.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define FE_TAG "FACE_ENG"

static const face_engine_ops_t *s_ops = NULL;
static void                    *s_ctx = NULL;
static SemaphoreHandle_t        s_lock = NULL;  /* 串行化 detect/extract（模型非线程安全） */

dal_err_t face_engine_set_ops(const face_engine_ops_t *ops, void *ctx)
{
    if (ops == NULL || ops->detect == NULL) {
        return DAL_ERR_INVALID;
    }
    if (s_ops != NULL) {
        return DAL_ERR_STATE;
    }
    s_ops = ops;
    s_ctx = ctx;
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return DAL_ERR_NO_MEM;
    if (ops->init) {
        dal_err_t ret = ops->init(ctx);
        if (ret != DAL_OK) {
            s_ops = NULL;
            s_ctx = NULL;
            vSemaphoreDelete(s_lock);
            s_lock = NULL;
            return ret;
        }
    }
    ESP_LOGI(FE_TAG, "engine ops installed");
    return DAL_OK;
}

bool face_engine_is_ready(void)
{
    return s_ops != NULL;
}

int face_engine_detect(const face_image_t *img,
                       face_box_t *out_boxes, uint8_t max_boxes)
{
    if (s_ops == NULL || img == NULL || out_boxes == NULL) return 0;
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    int n = s_ops->detect(s_ctx, img, out_boxes, max_boxes);
    if (s_lock) xSemaphoreGive(s_lock);
    return n;
}

dal_err_t face_engine_extract(const face_image_t *crop, face_feature_t *out_feat)
{
    if (s_ops == NULL || s_ops->extract == NULL || crop == NULL || out_feat == NULL)
        return DAL_ERR_UNSUPPORTED;
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    dal_err_t r = s_ops->extract(s_ctx, crop, out_feat);
    if (s_lock) xSemaphoreGive(s_lock);
    return r;
}

dal_err_t face_engine_compare(const face_feature_t *a,
                              const face_feature_t *b, float *out_score)
{
    if (s_ops == NULL || s_ops->compare == NULL || a == NULL || b == NULL || out_score == NULL)
        return DAL_ERR_UNSUPPORTED;
    /* compare 是纯数学（无模型状态），不需加锁 */
    return s_ops->compare(s_ctx, a, b, out_score);
}

/* ================================================================
 *  Stub 实现
 * ================================================================ */
static dal_err_t stub_init(void *ctx) { (void)ctx; return DAL_OK; }
static dal_err_t stub_deinit(void *ctx) { (void)ctx; return DAL_OK; }

static int stub_detect(void *ctx, const face_image_t *img,
                       face_box_t *out_boxes, uint8_t max_boxes)
{
    (void)ctx; (void)img; (void)out_boxes; (void)max_boxes;
    return 0;   /* 恒无人脸，流水线空跑 */
}

static const face_engine_ops_t s_stub_ops = {
    .init    = stub_init,
    .deinit  = stub_deinit,
    .detect  = stub_detect,
    .extract = NULL,
    .compare = NULL,
};

dal_err_t face_engine_install_stub(void)
{
    return face_engine_set_ops(&s_stub_ops, NULL);
}
