/**
 * @file    svc_face_detect.c
 * @brief   人脸检测服务 — 消费摄像头帧 → face_engine_detect → 输出人脸框
 *
 * @details 数据流:
 *          svc_camera_acquire_frame (共享 framebuffer, ref++)
 *          → memcpy 到私有 RGB888 buffer（纯拷贝，非格式转换）
 *          → svc_camera_release_frame（ref--, 早释放，共享帧仅持有 ~15ms）
 *          → face_engine_detect(私有 buffer, format=1=RGB888)  // ~100ms
 *          → g_q_det_to_ui (detect_to_ui_t, 人脸框)
 *
 *          私有 buffer 必要：detect 耗时 ~100ms，若全程持共享帧，3 个 BSP
 *          缓冲被 detect+UI+camera 占满，camera 采不到新帧 → UI 预览阻断。
 *          拷贝后早释放，共享帧仅持 ~15ms，camera 全速采集。
 *          用 RGB888 直传（esp-dl 预处理支持），不做 RGB565 转换。
 *
 *          feature 管线禁用（模型量化未恢复），不裁剪人脸。
 *
 * @author  xiamu
 * @version 2.2
 */

#include "svc_face_detect.h"
#include "svc_camera.h"
#include "svc_perm_manager.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "msg_queues.h"
#include "face_engine.h"

#define SVC_DET_TAG "SVC_DET"

/* 私有 detect buffer 上限（帧 800×640，RGB888=3B/px → ~1.5MB） */
#define DET_BUF_MAX_BYTES  (800u * 640u * 3u)

/* 人脸裁剪最小尺寸 */
#define FACE_MIN_SIZE  20

/* ================================================================
 *  人脸裁剪
 * ================================================================ */
#define FEAT_TARGET_SIZE 112

static uint8_t *crop_face_rgb888(const uint8_t *src, uint16_t sw, uint16_t sh,
                                  const face_box_t *box, uint16_t *ow, uint16_t *oh)
{
    uint16_t x = box->x, y = box->y, w = box->w, h = box->h;
    if (x >= sw || y >= sh || w < FACE_MIN_SIZE || h < FACE_MIN_SIZE) return NULL;
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    size_t rb = (size_t)w * 3;
    uint8_t *dst = (uint8_t *)heap_caps_malloc((size_t)h * rb, MALLOC_CAP_SPIRAM);
    if (dst == NULL) return NULL;
    for (uint16_t r = 0; r < h; r++)
        memcpy(dst + r * rb, src + ((size_t)(y + r) * sw + x) * 3, rb);
    *ow = w; *oh = h;
    return dst;
}

/** 最近邻缩放→112×112（MobileFaceNet），分配并返回新 buffer */
static uint8_t *resize_to_feat(const uint8_t *src, uint16_t sw, uint16_t sh)
{
    uint8_t *d = (uint8_t *)heap_caps_malloc(FEAT_TARGET_SIZE*FEAT_TARGET_SIZE*3, MALLOC_CAP_SPIRAM);
    if (d == NULL) return NULL;
    for (int y = 0; y < FEAT_TARGET_SIZE; y++) {
        const uint8_t *sr = src + (y * sh / FEAT_TARGET_SIZE) * sw * 3;
        uint8_t *dd = d + y * FEAT_TARGET_SIZE * 3;
        for (int x = 0; x < FEAT_TARGET_SIZE; x++, dd += 3) {
            const uint8_t *sp = sr + (x * sw / FEAT_TARGET_SIZE) * 3;
            dd[0] = sp[0]; dd[1] = sp[1]; dd[2] = sp[2];
        }
    }
    return d;
}

/* ================================================================
 *  静态变量
 * ================================================================ */

static TaskHandle_t s_worker = NULL;            /**< 检测任务句柄 */
static bool         s_inited = false;           /**< 初始化标志 */
static uint8_t     *s_det_buf = NULL;           /**< 私有 RGB888 buffer（PSRAM，懒分配） */
static size_t       s_det_buf_size = 0;         /**< 实际帧字节大小 */

/* ================================================================
 *  检测任务
 * ================================================================ */

static void s_detect_task(void *arg)
{
    (void)arg;

    /* 等候 face_engine 就绪（最多等 5 秒） */
    int retry = 0;
    while (!face_engine_is_ready() && retry < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        retry++;
    }
    if (!face_engine_is_ready()) {
        ESP_LOGE(SVC_DET_TAG, "face_engine not ready after 5s, exiting");
        s_worker = NULL;
        vTaskDelete(NULL);
        return;
    }

    face_box_t boxes[FACE_MAX_BOXES];
    uint32_t   frame_count = 0;
    uint32_t   face_count_total = 0;
    uint32_t   last_cam_seq = 0;

    ESP_LOGI(SVC_DET_TAG, "worker started");

    for (;;) {
        /* ----- 1. 取最新 camera 帧（ref++）----- */
        svc_frame_t fb;
        if (!svc_camera_acquire_frame(&fb, &last_cam_seq)) {
            vTaskDelay(pdMS_TO_TICKS(30));   /* 无新帧，让出 */
            continue;
        }

        /* 注册期间跳过人脸检测——注册有自己的固定裁剪提取路径 */
        if (svc_perm_manager_is_enrolling()) {
            svc_camera_release_frame(&fb);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        frame_count++;

        /* 懒分配私有 RGB888 buffer（按首帧尺寸，上限 DET_BUF_MAX_BYTES） */
        size_t need = (size_t)fb.width * fb.height * 3;
        if (s_det_buf == NULL || s_det_buf_size < need) {
            if (s_det_buf != NULL) {
                heap_caps_free(s_det_buf);
            }
            s_det_buf_size = (need > DET_BUF_MAX_BYTES) ? DET_BUF_MAX_BYTES : need;
            /* SIMD resize (xespv) 要求 16 字节对齐；heap_caps_malloc 不保证 */
            s_det_buf = (uint8_t *)heap_caps_aligned_alloc(16, s_det_buf_size,
                                                           MALLOC_CAP_SPIRAM);
            if (s_det_buf == NULL) {
                ESP_LOGE(SVC_DET_TAG, "det buf alloc %u failed", (unsigned)s_det_buf_size);
                svc_camera_release_frame(&fb);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
        }

        /* ----- 2. memcpy 共享帧到私有 buffer（纯拷贝，非格式转换）----- */
        size_t copy_bytes = (need <= s_det_buf_size) ? need : s_det_buf_size;
        memcpy(s_det_buf, fb.buf, copy_bytes);

        /* ----- 3. 早释放共享帧（detect 有副本，camera 可全速采集）----- */
        uint16_t fw = fb.width, fh = fb.height;
        svc_camera_release_frame(&fb);

        /* ----- 4. 在私有 buffer 上跑检测（RGB888 直传，adapter 不转换）----- */
        face_image_t img = {
            .data   = s_det_buf,
            .width  = fw,
            .height = fh,
            .format = 1,    /* RGB888 */
        };
        int detected = face_engine_detect(&img, boxes, FACE_MAX_BOXES);

        /* ----- 4a. 裁剪 + 缩放 112×112 → feature ----- */
        for (int i = 0; i < detected; i++) {
            uint16_t cw = 0, ch = 0;
            uint8_t *cb = crop_face_rgb888(s_det_buf, fw, fh, &boxes[i], &cw, &ch);
            if (cb == NULL) continue;
            uint8_t *fb = resize_to_feat(cb, cw, ch);
            heap_caps_free(cb);
            if (fb == NULL) continue;
            detect_to_feature_t dtf = {
                .buffer = fb, .frame_id = frame_count,
                .face_hash = (uint32_t)(frame_count * 10 + (uint32_t)i),
                .width = FEAT_TARGET_SIZE, .height = FEAT_TARGET_SIZE,
            };
            if (xQueueSend(g_q_det_to_feature, &dtf, 0) != pdTRUE)
                heap_caps_free(fb);
        }

        /* ----- 5. 发人脸框到 UI（含 0 人脸，通知清框）----- */
        detect_to_ui_t det_ui = {
            .frame_id   = frame_count,
            .face_count = (uint8_t)((detected > 0) ? detected : 0),
        };
        if (detected > 0) {
            memcpy(det_ui.boxes, boxes, (size_t)detected * sizeof(face_box_t));
            face_count_total += (uint32_t)detected;
        }
        xQueueSend(g_q_det_to_ui, &det_ui, 0);

        vTaskDelay(pdMS_TO_TICKS(10));  /* 让出 CPU */

        /* 周期性日志 */
        if ((frame_count & 0x3F) == 0) {
            ESP_LOGI(SVC_DET_TAG, "frames=%lu faces=%lu avg=%.1f",
                     (unsigned long)frame_count, (unsigned long)face_count_total,
                     frame_count ? (float)face_count_total / frame_count : 0.0f);
        }
    }
}

/* ================================================================
 *  公共 API
 * ================================================================ */

dal_err_t svc_face_detect_init(void)
{
    if (s_inited) {
        return DAL_ERR_STATE;
    }
    s_inited = true;
    ESP_LOGI(SVC_DET_TAG, "init ok");
    return DAL_OK;
}

dal_err_t svc_face_detect_start(void)
{
    if (!s_inited) {
        return DAL_ERR_STATE;
    }
    if (s_worker != NULL) {
        return DAL_OK;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(
        s_detect_task, "svc_detect",
        CONFIG_FACE_FACE_TASK_STACK, NULL,
        CONFIG_FACE_FACE_TASK_PRIO, &s_worker,
        0   /* Core 0 */
    );
    if (ret != pdPASS) {
        s_worker = NULL;
        return DAL_ERR_NO_MEM;
    }

    ESP_LOGI(SVC_DET_TAG, "task created");
    return DAL_OK;
}

dal_err_t svc_face_detect_stop(void)
{
    if (s_worker == NULL) {
        return DAL_OK;
    }
    TaskHandle_t h = s_worker;
    s_worker = NULL;
    vTaskDelete(h);
    s_inited = false;
    ESP_LOGI(SVC_DET_TAG, "stopped");
    return DAL_OK;
}
