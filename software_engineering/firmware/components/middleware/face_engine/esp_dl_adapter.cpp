/**
 * @file    esp_dl_adapter.cpp
 * @brief   esp-dl HumanFaceDetect → face_engine_ops_t 适配层
 *
 * @details 封装 esp-dl 的 HumanFaceDetect 为 face_engine_ops_t 接口，
 *          通过 face_engine_set_ops() 注册替代 stub。模型文件随组件分发。
 */
#include "face_engine.h"
#include "human_face_detect.hpp"
#include "face_feature_model.hpp"
#include "dl_image_define.hpp"
#include "esp_log.h"
#include <string.h>
#include <math.h>

#define ADAPTER_TAG "DL_ADAPT"

static HumanFaceDetect  *s_detect_model = NULL;
static FaceFeatureModel *s_feat_model   = NULL;

/* ---- C→C++ 适配 ---- */

static dal_err_t dl_init(void *ctx)
{
    (void)ctx;
    if (s_detect_model != NULL) return DAL_ERR_STATE;

    s_detect_model = new HumanFaceDetect();
    if (s_detect_model == NULL) return DAL_ERR_NO_MEM;

    s_feat_model = new FaceFeatureModel(true);
    if (s_feat_model == NULL) {
        delete s_detect_model; s_detect_model = NULL;
        return DAL_ERR_NO_MEM;
    }

    ESP_LOGI(ADAPTER_TAG, "engines ready (detect + feature)");
    return DAL_OK;
}

static dal_err_t dl_deinit(void *ctx)
{
    (void)ctx;
    if (s_detect_model) { delete s_detect_model; s_detect_model = NULL; }
    if (s_feat_model)   { delete s_feat_model;   s_feat_model   = NULL; }
    return DAL_OK;
}

static int dl_detect(void *ctx, const face_image_t *img,
                     face_box_t *out_boxes, uint8_t max_boxes)
{
    (void)ctx;
    if (s_detect_model == NULL || img == NULL || img->data == NULL || out_boxes == NULL) return 0;

    dl::image::pix_type_t input_fmt = (img->format == 1)
                                     ? dl::image::DL_IMAGE_PIX_TYPE_RGB888
                                     : dl::image::DL_IMAGE_PIX_TYPE_RGB565;

    /* 查询 MSR 模型原生输入尺寸，软件降采样到该尺寸使 ImagePreprocessor
     * 走 cvt_color（scale=1）而非 resize_nn→绕开 SIMD 栈溢出。 */
    static uint16_t s_msr_w = 0, s_msr_h = 0;
    static uint8_t *s_down_buf = NULL;
    if (s_msr_w == 0) {
        dl::Model *m = s_detect_model->get_raw_model(0);  // MSR model
        if (m) {
            dl::TensorBase *t = m->get_input();
            if (t && t->shape.size() >= 3)
                { s_msr_h = (uint16_t)t->shape[1]; s_msr_w = (uint16_t)t->shape[2]; }
        }
        if (s_msr_w == 0) { s_msr_w = 320; s_msr_h = 240; }  // fallback
        s_down_buf = (uint8_t *)heap_caps_malloc(
                        (size_t)s_msr_w * s_msr_h * 3, MALLOC_CAP_SPIRAM);
        ESP_LOGI(ADAPTER_TAG, "MSR native: %ux%u", s_msr_w, s_msr_h);
    }
    uint16_t sw = (uint16_t)img->width, sh = (uint16_t)img->height;
    const uint8_t *src = (const uint8_t *)img->data;
    for (int dy = 0; dy < s_msr_h; dy++) {
        const uint8_t *sr = src + (dy * sh / s_msr_h) * sw * 3;
        uint8_t *dd = s_down_buf + dy * s_msr_w * 3;
        for (int dx = 0; dx < s_msr_w; dx++, dd += 3) {
            const uint8_t *sp = sr + (dx * sw / s_msr_w) * 3;
            dd[0] = sp[0]; dd[1] = sp[1]; dd[2] = sp[2];
        }
    }
    dl::image::img_t dl_img = {
        .data = s_down_buf, .width = s_msr_w,
        .height = s_msr_h, .pix_type = input_fmt,
    };
    float bsx = (float)sw / s_msr_w, bsy = (float)sh / s_msr_h;

    static bool s_first = true;
    if (s_first) {
        s_first = false;
        s_detect_model->set_score_thr(0.1f, 0);
        s_detect_model->set_score_thr(0.1f, 1);
    }

    auto &res = s_detect_model->run(dl_img);
    static int dcnt = 0;
    if (dcnt++ % 10 == 0) ESP_LOGI(ADAPTER_TAG, "detect #%d: %u faces", dcnt, (unsigned)res.size());

    int count = 0;
    for (auto &r : res) {
        if (count >= (int)max_boxes) break;
        out_boxes[count].x     = (uint16_t)((float)r.box[0] * bsx);
        out_boxes[count].y     = (uint16_t)((float)r.box[1] * bsy);
        out_boxes[count].w     = (uint16_t)((float)(r.box[2] - r.box[0]) * bsx);
        out_boxes[count].h     = (uint16_t)((float)(r.box[3] - r.box[1]) * bsy);
        out_boxes[count].score = r.score;
        count++;
    }
    return count;
}

/* ---- 特征提取 ---- */

static dal_err_t dl_extract(void *ctx, const face_image_t *crop,
                             face_feature_t *out_feat)
{
    (void)ctx;
    if (s_feat_model == NULL || crop == NULL || out_feat == NULL) {
        return DAL_ERR_INVALID;
    }

    dl::image::img_t img = {
        .data      = (void *)crop->data,
        .width     = crop->width,
        .height    = crop->height,
        .pix_type  = dl::image::DL_IMAGE_PIX_TYPE_RGB888,
    };

    int dim = s_feat_model->run(img, out_feat->vec, FACE_FEATURE_DIM);
    if (dim <= 0) return DAL_ERR_HW;
    out_feat->dim = (uint16_t)dim;
    return DAL_OK;
}

/* ---- 余弦相似度 (纯数学, 不需要模型) ---- */

static dal_err_t dl_compare(void *ctx, const face_feature_t *a,
                             const face_feature_t *b, float *out_score)
{
    (void)ctx;
    if (a == NULL || b == NULL || out_score == NULL) return DAL_ERR_INVALID;

    uint16_t dim = a->dim < b->dim ? a->dim : b->dim;
    float dot = 0.0f;
    for (uint16_t i = 0; i < dim; i++) dot += a->vec[i] * b->vec[i];
    /* 诊断：打前 4 个元素 + dot */
    ESP_LOGI(ADAPTER_TAG, "cmp a=%.4f,%.4f,%.4f,%.4f  b=%.4f,%.4f,%.4f,%.4f dot=%.4f",
             a->vec[0], a->vec[1], a->vec[2], a->vec[3],
             b->vec[0], b->vec[1], b->vec[2], b->vec[3], dot);
    *out_score = (dot + 1.0f) * 0.5f;
    return DAL_OK;
}

/* ---- ops 注册 ---- */

static const face_engine_ops_t s_dl_ops = {
    .init    = dl_init,
    .deinit  = dl_deinit,
    .detect  = dl_detect,
    .extract = dl_extract,
    .compare = dl_compare,
};

dal_err_t face_engine_install_dl(void)
{
    return face_engine_set_ops(&s_dl_ops, NULL);
}
