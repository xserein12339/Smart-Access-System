/**
 * @file    bsp_camera_ov5647.c
 * @brief   OV5647 摄像头 BSP 实现 — 回调式帧推送
 *
 * @details 基于 PAL cam（封装 esp_video/CSI/V4L2），实现 dal_camera_ops_t：
 *          - init：从 board 获取共享 I2C 总线，调 pal_cam_init（参数来自
 *            bsp_config.h），pal_cam_init 内部已 STREAMON。
 *          - start_stream：创建/唤醒内部采集任务，循环 pal_cam_get_frame →
 *            转换为 dal_camera_frame_t → 调用 Service 注册的回调 → 等
 *            Service return_frame 归还。回调式推送，Service 不阻塞取帧。
 *          - stop_stream：停止采集任务循环。
 *          - return_frame：归还 PAL 帧缓冲（pal_cam_return_frame）。
 *          PAL 返回码经 dal_err_from_pal 翻译为 dal_err_t。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "bsp_camera_ov5647.h"
#include "dal_camera.h"
#include "dal_camera_interface.h"
#include "dal_pal_err.h"
#include "bsp_config.h"
#include "pal_cam.h"
#include "pal_i2c.h"
#include "board_v1.h"
#include "pal_log.h"
#include "osal_task.h"
#include "osal_semaphore.h"
#include "osal_mutex.h"
#include <string.h>

/* ---- 采集任务参数 ---- */
#define CAM_TASK_STACK      4096
#define CAM_TASK_PRIORITY   5
#define CAM_TASK_CORE       (-1)
/** 空闲等待信号量超时（ms）；OSAL 经 pdMS_TO_TICKS 转换，避免 portMAX_DELAY 溢出 */
#define CAM_IDLE_WAIT_MS    1000u

/* ---- BSP 私有上下文 ---- */
typedef struct {
    pal_cam_handle_t         pal_cam;      /**< PAL cam 句柄 */
    pal_cam_frame_t          pal_frame;    /**< 当前持有帧（待 return_frame 归还） */
    bool                     pal_frame_held; /**< 是否正持有未归还帧 */
    bool                     streaming;    /**< 是否在推流 */
    bool                     inited;       /**< 是否已初始化 */
    dal_camera_frame_cb_t    cb;           /**< Service 帧回调 */
    void                    *user_data;    /**< 回调用户数据 */
    osal_sem_t               start_sem;    /**< 唤醒采集任务开始/停止 */
    osal_mutex_t             mutex;        /**< 保护 streaming/cb/user_data/pal_frame_held */
    osal_task_handle_t       task;         /**< 采集任务句柄 */
} bsp_ov5647_ctx_t;

static bsp_ov5647_ctx_t s_ctx;

/* ---- PAL 格式 ↔ DAL 格式 ---- */
static pal_cam_fmt_t dal_to_pal_fmt(dal_camera_fmt_t fmt)
{
    switch (fmt) {
    case DAL_CAMERA_FMT_RGB888: return PAL_CAM_FMT_RGB888;
    case DAL_CAMERA_FMT_JPEG:   return PAL_CAM_FMT_JPEG;
    case DAL_CAMERA_FMT_RGB565:
    default:                    return PAL_CAM_FMT_RGB565;
    }
}

static dal_camera_fmt_t pal_to_dal_fmt(pal_cam_fmt_t fmt)
{
    switch (fmt) {
    case PAL_CAM_FMT_RGB888: return DAL_CAMERA_FMT_RGB888;
    case PAL_CAM_FMT_JPEG:   return DAL_CAMERA_FMT_JPEG;
    case PAL_CAM_FMT_RGB565:
    default:                 return DAL_CAMERA_FMT_RGB565;
    }
}

/* ================================================================
 *  采集任务：回调式帧推送
 * ================================================================ */
static void cam_capture_task(void *arg)
{
    bsp_ov5647_ctx_t *ctx = (bsp_ov5647_ctx_t *)arg;

    while (1) {
        /* 等待 start_stream 唤醒；空闲时周期性重检 streaming */
        (void)osal_sem_take(ctx->start_sem, CAM_IDLE_WAIT_MS);

        bool streaming = false;
        if (!osal_mutex_lock(ctx->mutex, 1000)) {
            continue;
        }
        streaming = ctx->streaming;
        osal_mutex_unlock(ctx->mutex);

        while (streaming) {
            /* 取一帧（pal_cam_get_frame 阻塞至帧就绪或超时） */
            pal_cam_frame_t pf = {0};
            int ret = pal_cam_get_frame(ctx->pal_cam, &pf);
            if (ret != 0) {
                PAL_LOGW("OV5647", "get_frame failed: %d", ret);
                osal_task_delay_ms(10);
                continue;
            }

            /* 持有帧，转换并投递回调 */
            if (!osal_mutex_lock(ctx->mutex, 1000)) {
                pal_cam_return_frame(ctx->pal_cam, &pf);
                continue;
            }
            ctx->pal_frame      = pf;
            ctx->pal_frame_held = true;
            dal_camera_frame_cb_t cb      = ctx->cb;
            void                *user_data = ctx->user_data;
            osal_mutex_unlock(ctx->mutex);

            if (cb != NULL) {
                dal_camera_frame_t dframe = {
                    .buf          = pf.buf,
                    .len          = pf.len,
                    .width        = pf.width,
                    .height       = pf.height,
                    .format       = pal_to_dal_fmt(pf.format),
                    .timestamp_us = (uint64_t)pf.timestamp.tv_sec * 1000000ULL
                                  + (uint64_t)pf.timestamp.tv_usec,
                    .frame_handle = pf._priv,
                };
                cb(user_data, &dframe);
            } else {
                /* 无回调则直接归还 PAL 帧 */
                pal_cam_return_frame(ctx->pal_cam, &pf);
                if (osal_mutex_lock(ctx->mutex, 1000)) {
                    ctx->pal_frame_held = false;
                    osal_mutex_unlock(ctx->mutex);
                }
            }

            /* 重新检查 streaming 状态 */
            if (!osal_mutex_lock(ctx->mutex, 1000)) {
                break;
            }
            streaming = ctx->streaming;
            osal_mutex_unlock(ctx->mutex);
        }
    }
}

/* ================================================================
 *  dal_camera_ops_t 实现
 * ================================================================ */
static dal_err_t ov5647_init(void *ctx_, const dal_camera_config_t *cfg)
{
    bsp_ov5647_ctx_t *ctx = (bsp_ov5647_ctx_t *)ctx_;
    if (ctx == NULL || cfg == NULL) {
        return DAL_ERR_INVALID;
    }
    if (ctx->inited) {
        return DAL_ERR_STATE;
    }

    pal_cam_config_t pal_cfg = {
        .i2c_bus       = (pal_i2c_bus_handle_t)board_i2c_get_bus(),
        .reset_pin     = BOARD_CAMERA_RESET_PIN,
        .pwdn_pin      = BOARD_CAMERA_PWDN_PIN,
        .dont_init_ldo = false,
        .format        = dal_to_pal_fmt(cfg->format),
        .fb_count      = cfg->fb_count,
        .mem_type      = PAL_CAM_MEM_MMAP,
        .vflip         = cfg->vflip,
        .hflip         = cfg->hflip,
    };
    if (pal_cfg.i2c_bus == NULL) {
        PAL_LOGE("OV5647", "shared I2C bus not ready");
        return DAL_ERR_STATE;
    }

    int ret = pal_cam_init(&ctx->pal_cam, &pal_cfg);
    if (ret != 0) {
        return dal_err_from_pal(ret);
    }

    ctx->inited = true;
    PAL_LOGI("OV5647", "camera initialized");
    return DAL_OK;
}

static dal_err_t ov5647_start_stream(void *ctx_, dal_camera_frame_cb_t cb, void *user_data)
{
    bsp_ov5647_ctx_t *ctx = (bsp_ov5647_ctx_t *)ctx_;
    if (ctx == NULL || !ctx->inited || cb == NULL) {
        return DAL_ERR_INVALID;
    }

    if (!osal_mutex_lock(ctx->mutex, 1000)) {
        return DAL_ERR_BUSY;
    }
    if (ctx->streaming) {
        osal_mutex_unlock(ctx->mutex);
        return DAL_ERR_STATE;
    }
    ctx->cb        = cb;
    ctx->user_data = user_data;
    ctx->streaming = true;
    osal_mutex_unlock(ctx->mutex);

    osal_sem_give(ctx->start_sem);   /* 唤醒采集任务 */
    PAL_LOGI("OV5647", "stream started");
    return DAL_OK;
}

static dal_err_t ov5647_stop_stream(void *ctx_)
{
    bsp_ov5647_ctx_t *ctx = (bsp_ov5647_ctx_t *)ctx_;
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }

    if (!osal_mutex_lock(ctx->mutex, 1000)) {
        return DAL_ERR_BUSY;
    }
    ctx->streaming = false;
    ctx->cb        = NULL;
    ctx->user_data = NULL;
    osal_mutex_unlock(ctx->mutex);

    osal_sem_give(ctx->start_sem);   /* 唤醒任务使其退出采集循环 */
    PAL_LOGI("OV5647", "stream stopped");
    return DAL_OK;
}

static dal_err_t ov5647_return_frame(void *ctx_, const dal_camera_frame_t *frame)
{
    bsp_ov5647_ctx_t *ctx = (bsp_ov5647_ctx_t *)ctx_;
    if (ctx == NULL || frame == NULL) {
        return DAL_ERR_INVALID;
    }

    if (!osal_mutex_lock(ctx->mutex, 1000)) {
        return DAL_ERR_BUSY;
    }
    if (!ctx->pal_frame_held) {
        osal_mutex_unlock(ctx->mutex);
        return DAL_ERR_STATE;
    }

    /* 用持有的 PAL 帧归还（_priv 已存） */
    pal_cam_frame_t pf = ctx->pal_frame;
    ctx->pal_frame_held = false;
    osal_mutex_unlock(ctx->mutex);

    return dal_err_from_pal(pal_cam_return_frame(ctx->pal_cam, &pf));
}

static dal_err_t ov5647_deinit(void *ctx_)
{
    bsp_ov5647_ctx_t *ctx = (bsp_ov5647_ctx_t *)ctx_;
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }

    ov5647_stop_stream(ctx_);
    dal_err_t ret = dal_err_from_pal(pal_cam_deinit(ctx->pal_cam));
    ctx->pal_cam = NULL;
    ctx->inited  = false;
    return ret;
}

static const dal_camera_ops_t s_ov5647_ops = {
    .init          = ov5647_init,
    .start_stream  = ov5647_start_stream,
    .stop_stream   = ov5647_stop_stream,
    .return_frame  = ov5647_return_frame,
    .deinit        = ov5647_deinit,
};

/* ================================================================
 *  对外初始化入口（自注册）
 * ================================================================ */
dal_err_t bsp_camera_ov5647_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));

    s_ctx.mutex = osal_mutex_create();
    if (s_ctx.mutex == NULL) {
        return DAL_ERR_NO_MEM;
    }
    s_ctx.start_sem = osal_sem_create_binary();
    if (s_ctx.start_sem == NULL) {
        return DAL_ERR_NO_MEM;
    }

    /* 用板级默认配置预初始化摄像头 */
    dal_camera_config_t cfg = {
        .width    = BOARD_CAMERA_FRAME_WIDTH,
        .height   = BOARD_CAMERA_FRAME_HEIGHT,
        .format   = DAL_CAMERA_FMT_RGB565,
        .fb_count = BOARD_CAMERA_BUFFER_COUNT,
        .hflip    = false,
        .vflip    = false,
    };
    dal_err_t ret = ov5647_init(&s_ctx, &cfg);
    if (ret != DAL_OK) {
        return ret;
    }

    /* 创建常驻采集任务 */
    s_ctx.task = osal_task_create("bsp_cam_capture", cam_capture_task, &s_ctx,
                                  CAM_TASK_STACK, CAM_TASK_PRIORITY, CAM_TASK_CORE);
    if (s_ctx.task == NULL) {
        ov5647_deinit(&s_ctx);
        return DAL_ERR_NO_MEM;
    }

    /* 自注册到 DAL */
    return dal_camera_register("main_cam", &s_ov5647_ops, &s_ctx);
}
