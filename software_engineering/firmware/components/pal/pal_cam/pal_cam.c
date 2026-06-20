/**
 * @file    pal_cam.c
 * @brief   PAL CAM 模块 - 实现（ESP-IDF esp_video V4L2 路径封装）
 *
 * C 语言实现 who_p4_cam.cpp 的初始化/取帧/还帧流程：
 *   esp_video_init → open → S_EXT_CTRLS(flip) → S_FMT → REQBUFS →
 *   QUERYBUF + mmap/heap_caps_malloc + QBUF → STREAMON
 */

#include "pal_cam.h"
#include "pal_i2c.h"

#include "esp_video_init.h"
#include "esp_video_device.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "hal/gpio_types.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  内部结构体
 * ================================================================ */

/** @brief PAL CAM 内部状态 */
typedef struct {
    int                  fd;          /**< V4L2 设备文件描述符 */
    pal_cam_fmt_t        format;      /**< 当前像素格式 */
    uint16_t             width;       /**< 帧宽 */
    uint16_t             height;      /**< 帧高 */
    uint8_t              fb_count;    /**< 帧缓冲数量 */
    pal_cam_buf_mem_t    mem_type;    /**< 帧缓冲内存类型 */
    struct v4l2_buffer  *v4l2_bufs;   /**< 每个帧缓冲对应的 v4l2_buffer（QBUF 用） */
    void               **fb_bufs;     /**< 每个帧缓冲的数据指针（mmap 或 malloc） */
    size_t              *fb_lens;     /**< 每个帧缓冲的总长度 */
    bool                *fb_mmapped;  /**< 该缓冲是否为 mmap（决定 munmap 还是 free） */
} pal_cam_internal_t;

/* ================================================================
 *  内部映射
 * ================================================================ */

/** @brief PAL 像素格式 → V4L2 fourcc */
static uint32_t pal_to_v4l2_fmt(pal_cam_fmt_t fmt)
{
    switch (fmt) {
    case PAL_CAM_FMT_RGB565: return V4L2_PIX_FMT_RGB565;
    case PAL_CAM_FMT_RGB888: return V4L2_PIX_FMT_RGB24;
    case PAL_CAM_FMT_JPEG:   return V4L2_PIX_FMT_JPEG;
    default:                 return V4L2_PIX_FMT_RGB565;
    }
}

/* ================================================================
 *  内部步骤
 * ================================================================ */

/**
 * @brief 设置传感器翻转（VFLIP/HFLIP）
 */
static int pal_cam_set_flip(pal_cam_internal_t *ctx, bool vflip, bool hflip)
{
    if (!vflip && !hflip) {
        return ESP_OK;
    }

    struct v4l2_ext_control ctrl = {0};
    if (vflip) {
        ctrl.id    = V4L2_CID_VFLIP;
        ctrl.value = 1;
        if (ioctl(ctx->fd, VIDIOC_S_EXT_CTRLS,
                  &(struct v4l2_ext_controls){
                      .ctrl_class = V4L2_CTRL_CLASS_USER,
                      .count      = 1,
                      .controls   = &ctrl,
                  }) != 0) {
            return ESP_FAIL;
        }
    }
    if (hflip) {
        ctrl.id    = V4L2_CID_HFLIP;
        ctrl.value = 1;
        if (ioctl(ctx->fd, VIDIOC_S_EXT_CTRLS,
                  &(struct v4l2_ext_controls){
                      .ctrl_class = V4L2_CTRL_CLASS_USER,
                      .count      = 1,
                      .controls   = &ctrl,
                  }) != 0) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

/**
 * @brief 设置像素格式，并记录传感器默认分辨率
 */
static int pal_cam_set_format(pal_cam_internal_t *ctx, pal_cam_fmt_t fmt)
{
    struct v4l2_format vfmt = {0};
    vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->fd, VIDIOC_G_FMT, &vfmt) != 0) {
        return ESP_FAIL;
    }
    /* 覆盖像素格式，分辨率沿用传感器默认 */
    vfmt.fmt.pix.pixelformat = pal_to_v4l2_fmt(fmt);
    if (ioctl(ctx->fd, VIDIOC_S_FMT, &vfmt) != 0) {
        return ESP_FAIL;
    }
    ctx->width  = (uint16_t)vfmt.fmt.pix.width;
    ctx->height = (uint16_t)vfmt.fmt.pix.height;
    return ESP_OK;
}

/**
 * @brief 申请帧缓冲并全部入队
 */
static int pal_cam_init_fbs(pal_cam_internal_t *ctx)
{
    enum v4l2_memory mem = (ctx->mem_type == PAL_CAM_MEM_MMAP)
                           ? V4L2_MEMORY_MMAP : V4L2_MEMORY_USERPTR;

    struct v4l2_requestbuffers req = {0};
    req.count  = ctx->fb_count;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = mem;
    if (ioctl(ctx->fd, VIDIOC_REQBUFS, &req) != 0) {
        return ESP_FAIL;
    }

    for (uint8_t i = 0; i < ctx->fb_count; i++) {
        struct v4l2_buffer *buf = &ctx->v4l2_bufs[i];
        memset(buf, 0, sizeof(*buf));
        buf->type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf->memory = mem;
        buf->index  = i;

        if (ioctl(ctx->fd, VIDIOC_QUERYBUF, buf) != 0) {
            return ESP_FAIL;
        }

        ctx->fb_lens[i] = buf->length;
        if (ctx->mem_type == PAL_CAM_MEM_MMAP) {
            ctx->fb_bufs[i]   = mmap(NULL, buf->length, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, ctx->fd, buf->m.offset);
            ctx->fb_mmapped[i] = true;
        } else {
            /* USERPTR：在 PSRAM 分配 DMA 对齐缓冲 */
            ctx->fb_bufs[i]    = heap_caps_malloc(buf->length,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            ctx->fb_mmapped[i] = false;
            buf->m.userptr     = (unsigned long)ctx->fb_bufs[i];
        }
        if (ctx->fb_bufs[i] == NULL) {
            return ESP_ERR_NO_MEM;
        }

        if (ioctl(ctx->fd, VIDIOC_QBUF, buf) != 0) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

/* ================================================================
 *  生命周期 API
 * ================================================================ */

int pal_cam_init(pal_cam_handle_t *handle, const pal_cam_config_t *cfg)
{
    if (handle == NULL || cfg == NULL || cfg->i2c_bus == NULL || cfg->fb_count < 2) {
        return ESP_ERR_INVALID_ARG;
    }

    pal_cam_internal_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ctx->fd       = -1;
    ctx->fb_count = cfg->fb_count;
    ctx->mem_type = cfg->mem_type;
    ctx->format   = cfg->format;

    ctx->v4l2_bufs  = calloc(cfg->fb_count, sizeof(struct v4l2_buffer));
    ctx->fb_bufs    = calloc(cfg->fb_count, sizeof(void *));
    ctx->fb_lens    = calloc(cfg->fb_count, sizeof(size_t));
    ctx->fb_mmapped = calloc(cfg->fb_count, sizeof(bool));
    if (ctx->v4l2_bufs == NULL || ctx->fb_bufs == NULL ||
        ctx->fb_lens == NULL || ctx->fb_mmapped == NULL) {
        pal_cam_deinit((pal_cam_handle_t)ctx);
        return ESP_ERR_NO_MEM;
    }

    /* ---- 1. esp_video 框架初始化（sensor 自动识别 + CSI + ISP） ---- */
    esp_video_init_csi_config_t csi_cfg = {
        .sccb_config = {
            .init_sccb = false,                  /* 复用外部 PAL I2C 总线 */
            .i2c_handle = (i2c_master_bus_handle_t)pal_i2c_get_bus_handle(cfg->i2c_bus),
            .freq       = 100000,
        },
        .reset_pin      = (cfg->reset_pin < 0) ? GPIO_NUM_NC : (gpio_num_t)cfg->reset_pin,
        .pwdn_pin       = (cfg->pwdn_pin  < 0) ? GPIO_NUM_NC : (gpio_num_t)cfg->pwdn_pin,
        .dont_init_ldo  = cfg->dont_init_ldo,
    };
    esp_video_init_config_t video_cfg = {0};
    video_cfg.csi = &csi_cfg;

    esp_err_t ret = esp_video_init(&video_cfg);
    if (ret != ESP_OK) {
        pal_cam_deinit((pal_cam_handle_t)ctx);
        return ret;
    }

    /* ---- 2. 打开 V4L2 设备 ---- */
    ctx->fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (ctx->fd < 0) {
        pal_cam_deinit((pal_cam_handle_t)ctx);
        return ESP_FAIL;
    }

    /* ---- 3. 翻转 / 像素格式 ---- */
    ret = pal_cam_set_flip(ctx, cfg->vflip, cfg->hflip);
    if (ret != ESP_OK) {
        pal_cam_deinit((pal_cam_handle_t)ctx);
        return ret;
    }
    ret = pal_cam_set_format(ctx, cfg->format);
    if (ret != ESP_OK) {
        pal_cam_deinit((pal_cam_handle_t)ctx);
        return ret;
    }

    /* ---- 4. 申请帧缓冲并入队 ---- */
    ret = pal_cam_init_fbs(ctx);
    if (ret != ESP_OK) {
        pal_cam_deinit((pal_cam_handle_t)ctx);
        return ret;
    }

    /* ---- 5. 启动采集流 ---- */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) != 0) {
        pal_cam_deinit((pal_cam_handle_t)ctx);
        return ESP_FAIL;
    }

    *handle = (pal_cam_handle_t)ctx;
    return ESP_OK;
}

int pal_cam_deinit(pal_cam_handle_t handle)
{
    pal_cam_internal_t *ctx = (pal_cam_internal_t *)handle;
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ctx->fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(ctx->fd, VIDIOC_STREAMOFF, &type);
    }

    /* 释放帧缓冲 */
    if (ctx->fb_bufs != NULL) {
        for (uint8_t i = 0; i < ctx->fb_count; i++) {
            if (ctx->fb_bufs[i] == NULL) {
                continue;
            }
            if (ctx->fb_mmapped != NULL && ctx->fb_mmapped[i]) {
                munmap(ctx->fb_bufs[i], ctx->fb_lens[i]);
            } else {
                heap_caps_free(ctx->fb_bufs[i]);
            }
        }
    }

    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }

    /* esp_video 框架反初始化（忽略返回值，资源已基本释放） */
    esp_video_deinit();

    free(ctx->v4l2_bufs);
    free(ctx->fb_bufs);
    free(ctx->fb_lens);
    free(ctx->fb_mmapped);
    free(ctx);
    return ESP_OK;
}

/* ================================================================
 *  帧采集 API
 * ================================================================ */

int pal_cam_get_frame(pal_cam_handle_t handle, pal_cam_frame_t *frame)
{
    pal_cam_internal_t *ctx = (pal_cam_internal_t *)handle;
    if (ctx == NULL || ctx->fd < 0 || frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct v4l2_buffer buf = {0};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = (ctx->mem_type == PAL_CAM_MEM_MMAP) ? V4L2_MEMORY_MMAP : V4L2_MEMORY_USERPTR;
    if (ioctl(ctx->fd, VIDIOC_DQBUF, &buf) != 0) {
        return ESP_FAIL;
    }

    int64_t us = esp_timer_get_time();
    frame->buf       = ctx->fb_bufs[buf.index];
    frame->len       = buf.bytesused;
    frame->width     = ctx->width;
    frame->height    = ctx->height;
    frame->format    = ctx->format;
    frame->timestamp.tv_sec  = (long)(us / 1000000);
    frame->timestamp.tv_usec = (long)(us % 1000000);
    frame->_priv     = (void *)(uintptr_t)buf.index;
    return ESP_OK;
}

int pal_cam_return_frame(pal_cam_handle_t handle, pal_cam_frame_t *frame)
{
    pal_cam_internal_t *ctx = (pal_cam_internal_t *)handle;
    if (ctx == NULL || ctx->fd < 0 || frame == NULL || frame->_priv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t index = (uint8_t)(uintptr_t)frame->_priv;
    if (index >= ctx->fb_count) {
        return ESP_ERR_INVALID_ARG;
    }

    if (ioctl(ctx->fd, VIDIOC_QBUF, &ctx->v4l2_bufs[index]) != 0) {
        return ESP_FAIL;
    }
    frame->_priv = NULL;
    return ESP_OK;
}
