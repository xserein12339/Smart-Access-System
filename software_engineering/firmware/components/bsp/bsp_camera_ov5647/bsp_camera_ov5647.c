/**
 * @file    bsp_camera_ov5647.c
 * @brief   OV5647 摄像头 BSP 实现 — V4L2 内联 + capture 式取帧（create 模式）
 *
 * @details 原 pal_cam 的 V4L2 封装已彻底展开内联到本文件，不再依赖
 *          pal_cam / pal_i2c。实现 dal_camera_ops_t 契约（纯 capture 式，
 *          BSP 不维护 OS 任务，采集由 service/main 层驱动）：
 *          - create()：memset 静态 ctx + 设 fd=-1，返回静态 ops（ctx 编译期
 *            已注入 ops->ctx），零硬件副作用。
 *          - init：esp_video_init(CSI, SCCB 复用 board 共享 I2C) → open
 *            /dev/video0(O_NONBLOCK) → S_EXT_CTRLS(flip) → S_FMT →
 *            REQBUFS + QUERYBUF + mmap/heap_caps_malloc + QBUF → STREAMON；
 *            最后通过 V4L2 ioctl(/dev/video20) 应用 ISP BLC 默认配置。
 *          - capture_frame：阻塞 DQBUF(O_NONBLOCK 轮询 + 超时) 取一帧，填
 *            dal_camera_frame_t 返回。无内部采集任务，由调用方在自有任务
 *            上下文循环调用。
 *          - return_frame：解码 frame_handle(index+1) → VIDIOC_QBUF 归还。
 *          - deinit：STREAMOFF + munmap/heap_caps_free 各 buf + close(fd)
 *            + esp_video_deinit。
 *          V4L2 流在 init 时 STREAMON 一次永久运行；capture_frame 在调用方
 *          任务上下文阻塞轮询 DQBUF，return_frame 归还帧缓冲。
 *          esp_err_t 在 ops 边界经 dal_err_from_esp 翻译为 dal_err_t。
 *
 * @author  xLumina
 * @version 2.1
 */
#include "bsp_camera_ov5647.h"
#include "dal_esp_err.h"
#include "board_v1_config.h"
#include "board_v1.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "esp_video_isp_ioctl.h"
#include "driver/i2c_master.h"
#include "hal/gpio_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <errno.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

static const char *TAG = "OV5647";

/* ---- RAW Bayer 捕获 DQBUF 超时重试（每次 10ms，总计 2s） ---- */
#define RAW_DQBUF_RETRIES       200
#define RAW_DUMP_BYTES_PER_LINE 64      /**< 每行 hex 输出的字节数 */

/* ================================================================
 *  帧缓冲内存类型（原 pal_cam_buf_mem_t，内联为 BSP 私有）
 * ================================================================ */
typedef enum {
    BSP_CAM_MEM_MMAP    = 0,   /**< 内核映射内存（驱动分配） */
    BSP_CAM_MEM_USERPTR = 1,   /**< 用户指针（PSRAM 分配，适合大缓冲） */
} bsp_cam_buf_mem_t;

/* ================================================================
 *  BSP 私有上下文（合并原 pal_cam_internal_t + BSP 同步原语）
 * ================================================================ */
typedef struct {
    /* ---- V4L2 / esp_video（原 pal_cam_internal_t） ---- */
    int                  fd;          /**< V4L2 设备文件描述符，-1 未打开 */
    dal_camera_fmt_t     format;      /**< 当前像素格式（DAL 业务枚举） */
    uint16_t             width;       /**< 帧宽 */
    uint16_t             height;      /**< 帧高 */
    uint8_t              fb_count;    /**< 帧缓冲数量 */
    bsp_cam_buf_mem_t    mem_type;    /**< 帧缓冲内存类型 */
    bool                 external_bufs; /**< true=外部注入缓冲（零拷贝），不 malloc/free */
    void               **ext_bufs;     /**< 外部缓冲指针数组（外部注入时使用） */
    size_t               ext_buf_size; /**< 外部缓冲单帧大小 */
    struct v4l2_buffer  *v4l2_bufs;   /**< 每个帧缓冲对应的 v4l2_buffer（QBUF 用） */
    void               **fb_bufs;     /**< 每个帧缓冲的数据指针（mmap 或 malloc） */
    size_t              *fb_lens;     /**< 每个帧缓冲的总长度 */
    bool                *fb_mmapped;  /**< 该缓冲是否为 mmap（决定 munmap 还是 free） */

    /* ---- 同步原语（capture 式：无内部任务/信号量/帧持有状态） ---- */
    SemaphoreHandle_t    mutex;       /**< 保护 capture/return 并发访问 v4l2_bufs */
    bool                 inited;      /**< 是否已初始化 */
} bsp_ov5647_ctx_t;

static bsp_ov5647_ctx_t s_ctx;

/* ---- 前向声明 ---- */
static dal_err_t ov5647_deinit(void *ctx_);

/* ================================================================
 *  格式映射（DAL 业务枚举 → V4L2 fourcc）
 * ================================================================ */
static uint32_t dal_to_v4l2_fmt(dal_camera_fmt_t fmt)
{
    switch (fmt) {
    case DAL_CAMERA_FMT_RGB565: return V4L2_PIX_FMT_RGB565;
    case DAL_CAMERA_FMT_RGB888: return V4L2_PIX_FMT_RGB24;
    case DAL_CAMERA_FMT_JPEG:   return V4L2_PIX_FMT_JPEG;
    default:                    return V4L2_PIX_FMT_RGB565;
    }
}

/* ================================================================
 *  V4L2 内部步骤（原 pal_cam 静态函数内联）
 * ================================================================ */

/**
 * @brief 设置传感器翻转（VFLIP/HFLIP）
 */
static esp_err_t ov5647_set_flip(bsp_ov5647_ctx_t *ctx, bool vflip, bool hflip)
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
            ESP_LOGE(TAG, "VFLIP failed, errno=%d", errno);
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
            ESP_LOGE(TAG, "HFLIP failed, errno=%d", errno);
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

/**
 * @brief 设置像素格式，并记录传感器返回的实际分辨率
 *
 * V4L2 规范：S_FMT 后驱动可能调整 pixelformat/width/height 到硬件支持
 * 的最接近值；必须验证返回的 pixelformat 是否与请求一致，否则后续按
 * 错误格式解析数据会导致花屏/错位。
 */
static esp_err_t ov5647_set_format(bsp_ov5647_ctx_t *ctx)
{
    struct v4l2_format vfmt = {0};
    vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->fd, VIDIOC_G_FMT, &vfmt) != 0) {
        ESP_LOGE(TAG, "G_FMT failed, errno=%d", errno);
        return ESP_FAIL;
    }
    uint32_t orig_pf = vfmt.fmt.pix.pixelformat;
    ESP_LOGI(TAG, "G_FMT: %ux%u fourcc=0x%08lx",
             vfmt.fmt.pix.width, vfmt.fmt.pix.height, (unsigned long)orig_pf);

    /* 覆盖像素格式，分辨率沿用传感器默认 */
    uint32_t req_pf = dal_to_v4l2_fmt(ctx->format);
    vfmt.fmt.pix.pixelformat = req_pf;
    if (ioctl(ctx->fd, VIDIOC_S_FMT, &vfmt) != 0) {
        ESP_LOGE(TAG, "S_FMT failed, errno=%d", errno);
        return ESP_FAIL;
    }
    ctx->width  = (uint16_t)vfmt.fmt.pix.width;
    ctx->height = (uint16_t)vfmt.fmt.pix.height;

    /* 校验驱动返回的 pixelformat 是否与请求一致 */
    if (vfmt.fmt.pix.pixelformat != req_pf) {
        ESP_LOGE(TAG, "S_FMT mismatch: req=0x%08lx got=0x%08lx",
                 (unsigned long)req_pf, (unsigned long)vfmt.fmt.pix.pixelformat);
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGI(TAG, "S_FMT ok: %ux%u", ctx->width, ctx->height);
    return ESP_OK;
}

/**
 * @brief 配置 ISP 中心裁剪（VIDIOC_S_SELECTION，须 STREAMON 前 stream off）
 *
 * @details sensor 输出 800×640，ISP 硬件 crop 到 800×480（中心裁剪 top=80）。
 *          CSI device 内部转 esp_isp_crop_config 实现硬件裁剪，camera 实际输出
 *          800×480，后续帧 len/width/height 均为 crop 后尺寸。crop 后更新 ctx 宽高。
 *          失败仅告警不致命（回退到 sensor 原始尺寸 800×640）。
 */
static esp_err_t ov5647_set_crop(bsp_ov5647_ctx_t *ctx)
{
#if BOARD_CAMERA_CROP_ENABLE
    struct v4l2_selection sel = {0};
    sel.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    sel.target = V4L2_SEL_TGT_CROP;
    sel.r.left = BOARD_CAMERA_CROP_LEFT;
    sel.r.top  = BOARD_CAMERA_CROP_TOP;
    sel.r.width  = BOARD_CAMERA_CROP_WIDTH;
    sel.r.height = BOARD_CAMERA_CROP_HEIGHT;

    if (ioctl(ctx->fd, VIDIOC_S_SELECTION, &sel) != 0) {
        ESP_LOGW(TAG, "ISP crop S_SELECTION failed, errno=%d (fallback to %ux%u)",
                 errno, ctx->width, ctx->height);
        return ESP_FAIL;
    }

    /* crop 后 G_FMT 确认实际输出尺寸 */
    struct v4l2_format vfmt = {0};
    vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->fd, VIDIOC_G_FMT, &vfmt) == 0) {
        ctx->width  = (uint16_t)vfmt.fmt.pix.width;
        ctx->height = (uint16_t)vfmt.fmt.pix.height;
    }
    ESP_LOGI(TAG, "ISP crop ok: %ux%u (top=%u)", ctx->width, ctx->height,
             (unsigned)BOARD_CAMERA_CROP_TOP);
#else
    (void)ctx;
#endif
    return ESP_OK;
}

/**
 * @brief 申请帧缓冲并全部入队
 */
static esp_err_t ov5647_init_fbs(bsp_ov5647_ctx_t *ctx)
{
    enum v4l2_memory mem = (ctx->mem_type == BSP_CAM_MEM_MMAP)
                           ? V4L2_MEMORY_MMAP : V4L2_MEMORY_USERPTR;

    struct v4l2_requestbuffers req = {0};
    req.count  = ctx->fb_count;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = mem;
    if (ioctl(ctx->fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "REQBUFS failed, errno=%d", errno);
        return ESP_FAIL;
    }

    for (uint8_t i = 0; i < ctx->fb_count; i++) {
        struct v4l2_buffer *buf = &ctx->v4l2_bufs[i];
        memset(buf, 0, sizeof(*buf));
        buf->type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf->memory = mem;
        buf->index  = i;

        if (ioctl(ctx->fd, VIDIOC_QUERYBUF, buf) != 0) {
            ESP_LOGE(TAG, "QUERYBUF[%u] failed, errno=%d", i, errno);
            return ESP_FAIL;
        }

        ctx->fb_lens[i] = buf->length;
        if (ctx->mem_type == BSP_CAM_MEM_MMAP) {
            ctx->fb_bufs[i]   = mmap(NULL, buf->length, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, ctx->fd, buf->m.offset);
            ctx->fb_mmapped[i] = true;
        } else {
            /* USERPTR：外部注入缓冲（零拷贝）或 PSRAM 分配 */
            if (ctx->external_bufs && ctx->ext_bufs != NULL) {
                ctx->fb_bufs[i]    = ctx->ext_bufs[i];
                ctx->fb_mmapped[i] = false;
            } else {
                ctx->fb_bufs[i]    = heap_caps_malloc(buf->length,
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
                ctx->fb_mmapped[i] = false;
            }
            buf->m.userptr = (unsigned long)ctx->fb_bufs[i];
        }
        if (ctx->fb_bufs[i] == NULL) {
            ESP_LOGE(TAG, "alloc fb[%u] failed", i);
            return ESP_ERR_NO_MEM;
        }

        if (ioctl(ctx->fd, VIDIOC_QBUF, buf) != 0) {
            ESP_LOGE(TAG, "QBUF[%u] failed, errno=%d", i, errno);
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

/**
 * @brief 阻塞取一帧图像（ops 方法）
 *
 * @details V4L2 设备以 O_NONBLOCK 打开，DQBUF 无就绪帧返回 EAGAIN。本函数
 *          在调用方任务上下文循环轮询 DQBUF，直至取到帧或超时。BSP 不创建
 *          采集任务，由 service/main 层在自有任务上下文调用。
 *          DQBUF 返回的 buf 含最新 index/flags/bytesused，必须回写到
 *          ctx->v4l2_bufs[buf.index]，否则 return_frame 用过期结构 QBUF 会失败。
 *          mutex 保护 DQBUF 与 v4l2_bufs 回写，避免与 return_frame 并发竞争；
 *          轮询等待期间释放 mutex，允许 return_frame 并发归还帧。
 *
 * @param[in]  ctx_       驱动上下文
 * @param[in]  timeout_ms 超时毫秒数，-1 永久阻塞；超时返回 DAL_ERR_TIMEOUT
 * @param[out] frame      帧描述（buf/len/width/height/format/timestamp/frame_handle）
 * @return DAL_OK 成功，DAL_ERR_TIMEOUT 超时无帧，DAL_ERR_INVALID 参数非法，
 *         DAL_ERR_BUSY mutex 竞争失败，DAL_ERR_HW DQBUF 硬件错误
 *
 * @warning 必须配对调用 return_frame 归还帧缓冲，否则 V4L2 队列耗尽。
 */
static dal_err_t ov5647_capture_frame(void *ctx_, int timeout_ms,
                                      dal_camera_frame_t *frame)
{
    bsp_ov5647_ctx_t *ctx = (bsp_ov5647_ctx_t *)ctx_;
    if (ctx == NULL || ctx->fd < 0 || frame == NULL) {
        return DAL_ERR_INVALID;
    }

    int64_t start_us = esp_timer_get_time();

    /* DQBUF 不持 mutex：V4L2 ioctl 内部自带并发保护，且 DQBUF 可能阻塞
     * 等待 DMA 出帧——若持 mutex 阻塞，return_frame 无法 QBUF 归还帧 →
     * V4L2 队列耗尽 → DMA 停流 → DQBUF 永久阻塞，死锁。
     * mutex 仅保护 v4l2_bufs 回写（DQBUF 成功后），return_frame 读它做 QBUF。 */
    for (;;) {
        struct v4l2_buffer buf = {0};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = (ctx->mem_type == BSP_CAM_MEM_MMAP)
                     ? V4L2_MEMORY_MMAP : V4L2_MEMORY_USERPTR;
        if (ioctl(ctx->fd, VIDIOC_DQBUF, &buf) != 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* 无就绪帧：检查超时（-1 永久阻塞），未超时短延时后重试 */
                if (timeout_ms >= 0) {
                    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
                    if (elapsed_ms >= timeout_ms) {
                        return DAL_ERR_TIMEOUT;
                    }
                }
                /* pdMS_TO_TICKS(1) 在 100Hz 下为 0，vTaskDelay(0) 不让低优先级
                 * 任务运行，故固定 1 tick 真正让出 CPU。 */
                vTaskDelay(1);
                continue;
            }
            ESP_LOGW(TAG, "DQBUF failed, errno=%d", errno);
            return DAL_ERR_HW;
        }

        /* DQBUF 成功：持锁回写 v4l2_bufs（return_frame 依赖此结构做 QBUF） */
        xSemaphoreTake(ctx->mutex, portMAX_DELAY);
        if (buf.index < ctx->fb_count) {
            ctx->v4l2_bufs[buf.index] = buf;
        }
        frame->buf          = ctx->fb_bufs[buf.index];
        frame->len          = buf.bytesused;
        frame->width        = ctx->width;
        frame->height       = ctx->height;
        frame->format       = ctx->format;
        frame->timestamp_us = (uint64_t)esp_timer_get_time();
        /* frame_handle 用 index+1 编码：index=0 时不能为 NULL（否则 return_frame
         * 把 NULL 当无效参数拒绝归还，导致帧永不归还、V4L2 队列耗尽只出 1 帧）。 */
        frame->frame_handle = (void *)(uintptr_t)(buf.index + 1U);
        xSemaphoreGive(ctx->mutex);
        return DAL_OK;
    }
}

/**
 * @brief 按 frame_handle(index+1 编码) 归还帧缓冲到 V4L2 队列（内部原语）
 *
 * @note 本函数不持 mutex，由调用方（return_frame）持锁后调用，避免重复
 *       持锁导致死锁。
 */
static dal_err_t ov5647_return_by_handle(bsp_ov5647_ctx_t *ctx, void *handle)
{
    if (ctx == NULL || ctx->fd < 0 || handle == NULL) {
        return DAL_ERR_INVALID;
    }
    uint8_t index = (uint8_t)(uintptr_t)handle - 1U;
    if (index >= ctx->fb_count) {
        return DAL_ERR_INVALID;
    }
    /* 用 DQBUF 回写的原始 buffer（含 flags 等）直接 QBUF */
    if (ioctl(ctx->fd, VIDIOC_QBUF, &ctx->v4l2_bufs[index]) != 0) {
        ESP_LOGW(TAG, "QBUF return failed: index=%u errno=%d",
                 (unsigned)index, errno);
        return DAL_ERR_HW;
    }
    return DAL_OK;
}

/* ================================================================
 *  dal_camera_ops_t 实现
 * ================================================================ */

/**
 * @brief 归还帧缓冲（ops 方法）
 *
 * @details 从 frame->frame_handle 解码 index+1 → index，持锁后调
 *          ov5647_return_by_handle 执行 QBUF。mutex 保护 v4l2_bufs 数组，
 *          避免与 capture_frame 并发访问。return_by_handle 不重复持锁，
 *          由本函数统一加锁。
 *
 * @param[in] ctx_  驱动上下文
 * @param[in] frame 帧描述（frame_handle 用于 BSP 内部归还）
 * @return DAL_OK 成功，DAL_ERR_INVALID 参数/handle 非法，
 *         DAL_ERR_BUSY mutex 竞争失败，DAL_ERR_HW QBUF 失败
 */
static dal_err_t ov5647_return_frame(void *ctx_, const dal_camera_frame_t *frame)
{
    bsp_ov5647_ctx_t *ctx = (bsp_ov5647_ctx_t *)ctx_;
    if (ctx == NULL || frame == NULL || frame->frame_handle == NULL) {
        return DAL_ERR_INVALID;
    }

    if (xSemaphoreTake(ctx->mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return DAL_ERR_BUSY;
    }
    /* 持锁调用 return_by_handle（内部 QBUF 不重复持锁，避免死锁） */
    dal_err_t ret = ov5647_return_by_handle(ctx, frame->frame_handle);
    xSemaphoreGive(ctx->mutex);
    return ret;
}

static dal_err_t ov5647_deinit(void *ctx_)
{
    bsp_ov5647_ctx_t *ctx = (bsp_ov5647_ctx_t *)ctx_;
    if (ctx == NULL) {
        return DAL_ERR_INVALID;
    }

    /* capture 式下无内部采集任务，直接 STREAMOFF 停止 V4L2 流。
     * 调用方应先停止自身采集循环再调 deinit。 */
    if (ctx->fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        (void)ioctl(ctx->fd, VIDIOC_STREAMOFF, &type);
    }

    /* 释放帧缓冲（外部注入缓冲不 free） */
    if (ctx->fb_bufs != NULL && !ctx->external_bufs) {
        for (uint8_t i = 0; i < ctx->fb_count; i++) {
            if (ctx->fb_bufs[i] == NULL) {
                continue;
            }
            if (ctx->fb_mmapped != NULL && ctx->fb_mmapped[i]) {
                munmap(ctx->fb_bufs[i], ctx->fb_lens[i]);
            } else {
                heap_caps_free(ctx->fb_bufs[i]);
            }
            ctx->fb_bufs[i] = NULL;
        }
    }

    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }

    /* esp_video 框架反初始化（忽略返回值，资源已基本释放） */
    esp_video_deinit();

    free(ctx->v4l2_bufs);
    ctx->v4l2_bufs = NULL;
    free(ctx->fb_bufs);
    ctx->fb_bufs = NULL;
    free(ctx->fb_lens);
    ctx->fb_lens = NULL;
    free(ctx->fb_mmapped);
    ctx->fb_mmapped = NULL;

    ctx->inited  = false;
    /* 保留 mutex 供再次 init 复用（幂等，与原 BSP 行为一致） */
    return DAL_OK;
}

static dal_err_t ov5647_init(void *ctx_, const dal_camera_config_t *cfg)
{
    bsp_ov5647_ctx_t *ctx = (bsp_ov5647_ctx_t *)ctx_;
    if (ctx == NULL || cfg == NULL || cfg->fb_count < 2) {
        return DAL_ERR_INVALID;
    }
    if (ctx->inited) {
        return DAL_ERR_STATE;
    }

    /* 从 board 取共享 I2C 总线，供 esp_video_init 的 SCCB 配置使用
     * （esp_video 内部自管 SCCB 设备句柄，BSP 不再直接写传感器 I2C） */
    i2c_master_bus_handle_t i2c_bus = (i2c_master_bus_handle_t)board_i2c_get_bus();
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "shared I2C bus not ready");
        return DAL_ERR_STATE;
    }
    ctx->fd       = -1;
    ctx->mem_type = ctx->external_bufs ? BSP_CAM_MEM_USERPTR : BSP_CAM_MEM_MMAP;
    /* fb_count：外部注入缓冲在 set_userptr_bufs 已设，cfg 不覆盖 */
    if (!ctx->external_bufs) {
        ctx->fb_count = cfg->fb_count;
    }
    ctx->format   = cfg->format;
    ctx->width    = cfg->width;
    ctx->height   = cfg->height;

    /* 同步原语（幂等：再次 init 时复用）。capture 式仅保留 mutex 保护
     * capture/return 并发，无信号量/任务。 */
    if (ctx->mutex == NULL) {
        ctx->mutex = xSemaphoreCreateMutex();
    }
    if (ctx->mutex == NULL) {
        ov5647_deinit(ctx_);
        return DAL_ERR_NO_MEM;
    }

    /* 分配帧缓冲管理数组 */
    ctx->v4l2_bufs  = calloc(cfg->fb_count, sizeof(struct v4l2_buffer));
    ctx->fb_bufs    = calloc(cfg->fb_count, sizeof(void *));
    ctx->fb_lens    = calloc(cfg->fb_count, sizeof(size_t));
    ctx->fb_mmapped = calloc(cfg->fb_count, sizeof(bool));
    if (ctx->v4l2_bufs == NULL || ctx->fb_bufs == NULL ||
        ctx->fb_lens == NULL || ctx->fb_mmapped == NULL) {
        ov5647_deinit(ctx_);
        return DAL_ERR_NO_MEM;
    }

    /* ---- 1. esp_video 框架初始化（sensor 自动识别 + CSI + ISP） ---- */
    esp_video_init_csi_config_t csi_cfg = {
        .sccb_config = {
            .init_sccb  = false,                  /* 复用外部共享 I2C 总线 */
            .i2c_handle = i2c_bus,
            .freq       = BOARD_CAMERA_SCCB_FREQ_HZ,
        },
        .reset_pin      = (BOARD_CAMERA_RESET_PIN < 0) ? GPIO_NUM_NC : (gpio_num_t)BOARD_CAMERA_RESET_PIN,
        .pwdn_pin       = (BOARD_CAMERA_PWDN_PIN  < 0) ? GPIO_NUM_NC : (gpio_num_t)BOARD_CAMERA_PWDN_PIN,
        .dont_init_ldo  = false,
    };
    esp_video_init_config_t video_cfg = {0};
    video_cfg.csi = &csi_cfg;

    /* 仅初始化 MIPI CSI + ISP，避免 esp_video_init(ALL) 触发 USB UVC 分支
     * （config->usb_uvc 为 NULL 时 create_usb_uvc_video_device 解引用崩溃）。
     * DVP/SPI/USB_UVC/H264/JPEG/MOTOR 本板不用，不初始化。 */
    esp_err_t ret = esp_video_init_with_flags(&video_cfg,
                                              ESP_VIDEO_INIT_FLAGS_MIPI_CSI |
                                              ESP_VIDEO_INIT_FLAGS_ISP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_video_init failed: %s", esp_err_to_name(ret));
        ov5647_deinit(ctx_);
        return dal_err_from_esp(ret);
    }

    /* ---- 2. 打开 V4L2 设备（O_NONBLOCK）----
     * 非阻塞 DQBUF：无帧返回 EAGAIN，capture_frame 在调用方任务上下文轮询。
     * V4L2 流在 init 时 STREAMON 一次永久运行，capture/return 不重置流。 */
    ctx->fd = open(BOARD_CAMERA_DEVICE_PATH, O_RDONLY | O_NONBLOCK);
    if (ctx->fd < 0) {
        ESP_LOGE(TAG, "open %s failed, errno=%d", BOARD_CAMERA_DEVICE_PATH, errno);
        ov5647_deinit(ctx_);
        return DAL_ERR_HW;
    }
    ESP_LOGI(TAG, "device opened, fd=%d", ctx->fd);

    /* ---- 3. 翻转 / 像素格式 ---- */
    ret = ov5647_set_flip(ctx, cfg->vflip, cfg->hflip);
    if (ret != ESP_OK) {
        ov5647_deinit(ctx_);
        return dal_err_from_esp(ret);
    }
    ret = ov5647_set_format(ctx);
    if (ret != ESP_OK) {
        ov5647_deinit(ctx_);
        return dal_err_from_esp(ret);
    }

    /* ISP 中心裁剪（须 REQBUFS/STREAMON 前，stream off 状态） */
    (void)ov5647_set_crop(ctx);

    /* ---- 4. 申请帧缓冲并入队 ---- */
    ret = ov5647_init_fbs(ctx);
    if (ret != ESP_OK) {
        ov5647_deinit(ctx_);
        return dal_err_from_esp(ret);
    }

    /* ---- 5. 启动采集流 ---- */
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "STREAMON failed, errno=%d", errno);
        ov5647_deinit(ctx_);
        return DAL_ERR_HW;
    }

    ctx->inited = true;

#if CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER
    extern bool esp_video_isp_pipeline_is_initialized(void);
    ESP_LOGI(TAG, "ISP pipeline: %s",
             esp_video_isp_pipeline_is_initialized() ? "ACTIVE" : "NOT active");
#endif

    /* ---- 6. BLC 默认配置（暂跳过）----
     * ESP32-P4 v1.3 的 ISP 硬件无 BLC 模块（需 rev3.0+，CONFIG_ESP32P4_REV_MIN_FULL<300
     * 时 esp_video 不编译 ISP BLC 控件，ioctl 返 ESP_ERR_NOT_SUPPORTED）。
     * esp_video 也未通过 V4L2 暴露传感器寄存器写(ESP_CAM_SENSOR_IOC_S_REG)。
     * 故 ISP BLC 与传感器 I2C BLC 当前均不可用，使用 OV5647 出厂默认 BLC。
     * 后续 rev3.0+ 芯片或改用传感器 I2C 直写时再启用。 */
#if 0
    {
        bsp_cam_blc_config_t blc_cfg = {
            .enable               = true,
            .stretch_enable       = false,
            .top_left_offset      = 2,
            .top_right_offset     = 2,
            .bottom_left_offset   = 2,
            .bottom_right_offset  = 2,
        };
        dal_err_t blc_ret = bsp_camera_ov5647_set_blc(&blc_cfg);
        if (blc_ret != DAL_OK) {
            ESP_LOGW(TAG, "default ISP BLC set failed: %d", blc_ret);
        } else {
            ESP_LOGI(TAG, "default ISP BLC applied (target=0x02)");
        }
    }
#endif

    /* capture 式：无内部采集任务，采集循环由 service/main 层在自有任务
     * 上下文调用 capture_frame 驱动。 */
    ESP_LOGI(TAG, "camera initialized: %ux%u", ctx->width, ctx->height);
    return DAL_OK;
}

/* ================================================================
 *  静态 ops + create 入口（仅绑定，不注册、不初始化硬件）
 * ================================================================ */
static dal_camera_ops_t s_ops = {
    .init          = ov5647_init,
    .capture_frame = ov5647_capture_frame,
    .return_frame  = ov5647_return_frame,
    .deinit        = ov5647_deinit,
    .ctx           = &s_ctx,
};

/* ---- 零拷贝：外部注入 USERPTR 缓冲（在 init 前调用） ---- */

void bsp_camera_ov5647_set_userptr_bufs(void **bufs, uint8_t count, size_t buf_size)
{
    s_ctx.external_bufs = true;
    s_ctx.ext_bufs      = bufs;
    s_ctx.ext_buf_size  = buf_size;
    s_ctx.fb_count      = count;
    /* mem_type 由 init 内部按 external_bufs 走 USERPTR 路径 */
}

dal_camera_ops_t *bsp_camera_ov5647_create(void)
{
    /* 非硬件字段初值：fd=-1（ctx 已编译期注入 ops->ctx） */
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.fd = -1;

    return &s_ops;
}

/* ================================================================
 *  ISP 调优（V4L2 ioctl 覆盖 /dev/video20）
 *
 *  保留 ISP 自动控制器（自动 3A）。通过 VIDIOC_S_EXT_CTRLS 在运行时覆盖
 *  各 ISP 模块。BLC 不被控制器自适应，覆盖稳定；BF/CCM/Gamma/WB 会被
 *  控制器按增益/亮度自适应，运行时覆盖为一次性。
 *
 *  驱动仅认 v4l2_ext_control 的 p_u8 + size 字段（不支持 value/string）。
 * ================================================================ */

/**
 * @brief 打开 ISP 设备（/dev/video20，O_RDWR）
 * @return >=0 文件描述符；<0 失败（-errno）
 */
static int isp_open(void)
{
    int fd = open(ESP_VIDEO_ISP1_DEVICE_NAME, O_RDWR);
    if (fd < 0) {
        ESP_LOGE(TAG, "ISP: open %s failed, errno=%d", ESP_VIDEO_ISP1_DEVICE_NAME, errno);
    }
    return fd;
}

/**
 * @brief 设置一个 ISP 控件（VIDIOC_S_EXT_CTRLS）
 *
 * @param id   V4L2_CID_USER_ESP_ISP_* 控件 ID
 * @param data 控件结构体指针
 * @param size 结构体大小
 * @return DAL_OK 成功，其他 dal_err_t 失败
 */
static dal_err_t isp_set_ctrl(int id, const void *data, size_t size)
{
    if (data == NULL || size == 0) {
        return DAL_ERR_INVALID;
    }

    int fd = isp_open();
    if (fd < 0) {
        return DAL_ERR_HW;
    }

    struct v4l2_ext_control ctrl = {
        .id   = id,
        .p_u8 = (uint8_t *)data,
        .size = size,
    };
    struct v4l2_ext_controls controls = {
        .ctrl_class = V4L2_CTRL_CLASS_USER,
        .count      = 1,
        .controls   = &ctrl,
    };

    esp_err_t ret = (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) == 0) ? ESP_OK : ESP_FAIL;
    close(fd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ISP: S_EXT_CTRLS id=0x%x failed, errno=%d", id, errno);
        return DAL_ERR_HW;
    }
    return DAL_OK;
}

dal_err_t bsp_camera_ov5647_set_blc(const bsp_cam_blc_config_t *cfg)
{
    if (!s_ctx.inited) {
        ESP_LOGE(TAG, "BLC: camera not initialized");
        return DAL_ERR_STATE;
    }

    /* bsp_cam_blc_config_t 与 esp_video_isp_blc_t 字段完全一致，可 memcpy */
    esp_video_isp_blc_t blc;
    if (cfg == NULL) {
        memset(&blc, 0, sizeof(blc));   /* 全零 = 禁用 */
    } else {
        memcpy(&blc, cfg, sizeof(blc));
    }

    dal_err_t ret = isp_set_ctrl(V4L2_CID_USER_ESP_ISP_BLC, &blc, sizeof(blc));
    if (ret != DAL_OK) {
        ESP_LOGE(TAG, "BLC: ISP ioctl failed: %d", ret);
        return ret;
    }

    if (cfg == NULL || !cfg->enable) {
        ESP_LOGI(TAG, "BLC: ISP BLC disabled");
    } else {
        ESP_LOGI(TAG, "BLC: ISP BLC enabled, offsets=[%u,%u,%u,%u]",
                 (unsigned)cfg->top_left_offset, (unsigned)cfg->top_right_offset,
                 (unsigned)cfg->bottom_left_offset, (unsigned)cfg->bottom_right_offset);
    }
    return DAL_OK;
}

dal_err_t bsp_camera_ov5647_set_bf(const bsp_cam_bf_config_t *cfg)
{
    if (!s_ctx.inited) {
        return DAL_ERR_STATE;
    }
    esp_video_isp_bf_t bf;
    if (cfg == NULL) {
        memset(&bf, 0, sizeof(bf));
    } else {
        if (cfg->enable && (cfg->level < 2 || cfg->level > 20)) {
            ESP_LOGE(TAG, "BF: level %u out of [2,20]", (unsigned)cfg->level);
            return DAL_ERR_INVALID;
        }
        memcpy(&bf, cfg, sizeof(bf));
    }
    return isp_set_ctrl(V4L2_CID_USER_ESP_ISP_BF, &bf, sizeof(bf));
}

dal_err_t bsp_camera_ov5647_set_ccm(const bsp_cam_ccm_config_t *cfg)
{
    if (!s_ctx.inited) {
        return DAL_ERR_STATE;
    }
    esp_video_isp_ccm_t ccm;
    if (cfg == NULL) {
        memset(&ccm, 0, sizeof(ccm));
    } else {
        memcpy(&ccm, cfg, sizeof(ccm));
    }
    return isp_set_ctrl(V4L2_CID_USER_ESP_ISP_CCM, &ccm, sizeof(ccm));
}

dal_err_t bsp_camera_ov5647_set_gamma(const bsp_cam_gamma_config_t *cfg)
{
    if (!s_ctx.inited) {
        return DAL_ERR_STATE;
    }
    esp_video_isp_gamma_t gamma;
    if (cfg == NULL) {
        memset(&gamma, 0, sizeof(gamma));
    } else {
        memcpy(&gamma, cfg, sizeof(gamma));
    }
    return isp_set_ctrl(V4L2_CID_USER_ESP_ISP_GAMMA, &gamma, sizeof(gamma));
}

dal_err_t bsp_camera_ov5647_set_wb(const bsp_cam_wb_config_t *cfg)
{
    if (!s_ctx.inited) {
        return DAL_ERR_STATE;
    }
    esp_video_isp_wb_t wb;
    if (cfg == NULL) {
        memset(&wb, 0, sizeof(wb));
    } else {
        memcpy(&wb, cfg, sizeof(wb));
    }
    return isp_set_ctrl(V4L2_CID_USER_ESP_ISP_WB, &wb, sizeof(wb));
}

/* ================================================================
 *  RAW Bayer 帧捕获（BLC 标定用，原 pal_cam_capture_raw_frame 内联）
 * ================================================================ */

/**
 * @brief 捕获一帧 Bayer RAW8 数据（暂停当前流，采集完毕恢复）
 *
 * @details 内部流程：STREAMOFF → 释放旧帧缓冲 → 切换 SRGGB8 → 采集一帧 →
 *          恢复原格式 → 重新申请原始帧缓冲 → STREAMON。全程约 200~500ms 无帧。
 */
static dal_err_t ov5647_capture_raw_frame(bsp_ov5647_ctx_t *ctx,
                                           uint8_t *buf, size_t len, size_t *out_len)
{
    if (ctx == NULL || ctx->fd < 0 || buf == NULL || out_len == NULL) {
        return DAL_ERR_INVALID;
    }

    /* ---- 1. 保存原始格式和分辨率 ---- */
    uint32_t orig_pf;
    uint32_t orig_w, orig_h;
    {
        struct v4l2_format vfmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
        if (ioctl(ctx->fd, VIDIOC_G_FMT, &vfmt) != 0) {
            ESP_LOGE(TAG, "RAW: G_FMT save failed, errno=%d", errno);
            return DAL_ERR_HW;
        }
        orig_pf = vfmt.fmt.pix.pixelformat;
        orig_w  = vfmt.fmt.pix.width;
        orig_h  = vfmt.fmt.pix.height;
        ESP_LOGI(TAG, "RAW: saving fmt=0x%08lx %ux%u",
                 (unsigned long)orig_pf, (unsigned)orig_w, (unsigned)orig_h);
    }

    /* ---- 2. STREAMOFF + 释放旧帧缓冲 ---- */
    {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        (void)ioctl(ctx->fd, VIDIOC_STREAMOFF, &type);
    }
    for (uint8_t i = 0; i < ctx->fb_count; i++) {
        if (ctx->fb_bufs[i] != NULL) {
            if (ctx->fb_mmapped != NULL && ctx->fb_mmapped[i]) {
                munmap(ctx->fb_bufs[i], ctx->fb_lens[i]);
            } else {
                heap_caps_free(ctx->fb_bufs[i]);
            }
            ctx->fb_bufs[i] = NULL;
        }
    }
    {
        struct v4l2_requestbuffers req = {
            .count  = 0,
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
        };
        (void)ioctl(ctx->fd, VIDIOC_REQBUFS, &req);
    }

    /* ---- 3. 切换到 RAW8 Bayer 格式 ---- */
    {
        struct v4l2_format vfmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
        vfmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SRGGB8;
        if (ioctl(ctx->fd, VIDIOC_S_FMT, &vfmt) != 0) {
            ESP_LOGE(TAG, "RAW: S_FMT SRGGB8 failed, errno=%d", errno);
            goto restore;
        }
        /* 读回驱动实际设置的格式 */
        memset(&vfmt, 0, sizeof(vfmt));
        vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(ctx->fd, VIDIOC_G_FMT, &vfmt) != 0) {
            ESP_LOGE(TAG, "RAW: G_FMT verify failed, errno=%d", errno);
            goto restore;
        }
        ESP_LOGI(TAG, "RAW: S_FMT ok, fmt=0x%08lx %ux%u size=%u",
                 (unsigned long)vfmt.fmt.pix.pixelformat,
                 vfmt.fmt.pix.width, vfmt.fmt.pix.height,
                 vfmt.fmt.pix.sizeimage);

        if (vfmt.fmt.pix.pixelformat != V4L2_PIX_FMT_SRGGB8) {
            ESP_LOGW(TAG, "RAW: fmt negotiated to 0x%08lx (expected 0x%08lx)",
                     (unsigned long)vfmt.fmt.pix.pixelformat,
                     (unsigned long)V4L2_PIX_FMT_SRGGB8);
        }
    }

    /* ---- 4. 申请 1 个 RAW 帧缓冲 ---- */
    {
        struct v4l2_requestbuffers req = {
            .count  = 1,
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
        };
        if (ioctl(ctx->fd, VIDIOC_REQBUFS, &req) != 0) {
            ESP_LOGE(TAG, "RAW: REQBUFS(1) failed, errno=%d", errno);
            goto restore;
        }

        struct v4l2_buffer raw_buf = {0};
        raw_buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        raw_buf.memory = V4L2_MEMORY_MMAP;
        raw_buf.index  = 0;
        if (ioctl(ctx->fd, VIDIOC_QUERYBUF, &raw_buf) != 0) {
            ESP_LOGE(TAG, "RAW: QUERYBUF failed, errno=%d", errno);
            goto restore;
        }

        void *raw_ptr = mmap(NULL, raw_buf.length, PROT_READ | PROT_WRITE,
                             MAP_SHARED, ctx->fd, raw_buf.m.offset);
        if (raw_ptr == MAP_FAILED) {
            ESP_LOGE(TAG, "RAW: mmap failed, errno=%d", errno);
            goto restore;
        }

        if (ioctl(ctx->fd, VIDIOC_QBUF, &raw_buf) != 0) {
            ESP_LOGE(TAG, "RAW: QBUF failed, errno=%d", errno);
            munmap(raw_ptr, raw_buf.length);
            goto restore;
        }

        /* ---- 5. STREAMON + DQBUF ---- */
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) != 0) {
            ESP_LOGE(TAG, "RAW: STREAMON failed, errno=%d", errno);
            munmap(raw_ptr, raw_buf.length);
            goto restore;
        }

        /* DQBUF 超时重试（流刚启动，首帧需要时间） */
        struct v4l2_buffer dqbuf = {0};
        int dq_ret = -1;
        for (int retry = 0; retry < RAW_DQBUF_RETRIES; retry++) {
            memset(&dqbuf, 0, sizeof(dqbuf));
            dqbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            dqbuf.memory = V4L2_MEMORY_MMAP;
            dq_ret = ioctl(ctx->fd, VIDIOC_DQBUF, &dqbuf);
            if (dq_ret == 0) break;
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGE(TAG, "RAW: DQBUF failed, errno=%d", errno);
                break;
            }
            usleep(10000);  /* 10ms */
        }
        if (dq_ret != 0) {
            ESP_LOGE(TAG, "RAW: DQBUF timeout after %d retries", RAW_DQBUF_RETRIES);
            {
                enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                (void)ioctl(ctx->fd, VIDIOC_STREAMOFF, &t);
            }
            munmap(raw_ptr, raw_buf.length);
            goto restore_no_dqbuf;
        }

        /* 拷贝 RAW 数据到用户缓冲区 */
        size_t copy_len = dqbuf.bytesused;
        if (copy_len > len) {
            ESP_LOGW(TAG, "RAW: truncating %u -> %u bytes",
                     (unsigned)copy_len, (unsigned)len);
            copy_len = len;
        }
        memcpy(buf, raw_ptr, copy_len);
        *out_len = copy_len;
        ESP_LOGI(TAG, "RAW: captured %u bytes (%ux%u Bayer)",
                 (unsigned)copy_len, (unsigned)orig_w, (unsigned)orig_h);

        /* QBUF 归还 */
        (void)ioctl(ctx->fd, VIDIOC_QBUF, &dqbuf);

        /* STREAMOFF + 释放 RAW 缓冲 */
        {
            enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            (void)ioctl(ctx->fd, VIDIOC_STREAMOFF, &t);
        }
        munmap(raw_ptr, raw_buf.length);
    }

restore_no_dqbuf:
    {
        struct v4l2_requestbuffers req = {
            .count  = 0,
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
        };
        (void)ioctl(ctx->fd, VIDIOC_REQBUFS, &req);
    }

restore:
    /* ---- 恢复原始格式 ---- */
    {
        struct v4l2_format vfmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
        vfmt.fmt.pix.pixelformat = orig_pf;
        vfmt.fmt.pix.width       = orig_w;
        vfmt.fmt.pix.height      = orig_h;
        if (ioctl(ctx->fd, VIDIOC_S_FMT, &vfmt) != 0) {
            ESP_LOGE(TAG, "RAW: restore S_FMT failed, errno=%d", errno);
            return DAL_ERR_HW;
        }
        ESP_LOGI(TAG, "RAW: restored fmt=0x%08lx %ux%u",
                 (unsigned long)orig_pf, (unsigned)orig_w, (unsigned)orig_h);
    }

    /* ---- 恢复原始帧缓冲（复用 ov5647_init_fbs 逻辑，但内存类型按 ctx->mem_type） ---- */
    {
        enum v4l2_memory mem = (ctx->mem_type == BSP_CAM_MEM_MMAP)
                               ? V4L2_MEMORY_MMAP : V4L2_MEMORY_USERPTR;

        struct v4l2_requestbuffers req = {
            .count  = ctx->fb_count,
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = mem,
        };
        if (ioctl(ctx->fd, VIDIOC_REQBUFS, &req) != 0) {
            ESP_LOGE(TAG, "RAW: restore REQBUFS failed, errno=%d", errno);
            return DAL_ERR_HW;
        }

        for (uint8_t i = 0; i < ctx->fb_count; i++) {
            struct v4l2_buffer *v4l2_buf = &ctx->v4l2_bufs[i];
            memset(v4l2_buf, 0, sizeof(*v4l2_buf));
            v4l2_buf->type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            v4l2_buf->memory = mem;
            v4l2_buf->index  = i;

            if (ioctl(ctx->fd, VIDIOC_QUERYBUF, v4l2_buf) != 0) {
                ESP_LOGE(TAG, "RAW: restore QUERYBUF[%u] failed", i);
                return DAL_ERR_HW;
            }

            ctx->fb_lens[i] = v4l2_buf->length;
            if (ctx->mem_type == BSP_CAM_MEM_MMAP) {
                ctx->fb_bufs[i] = mmap(NULL, v4l2_buf->length,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED, ctx->fd,
                                       v4l2_buf->m.offset);
                ctx->fb_mmapped[i] = true;
            } else {
                ctx->fb_bufs[i] = heap_caps_malloc(v4l2_buf->length,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
                ctx->fb_mmapped[i] = false;
                v4l2_buf->m.userptr = (unsigned long)ctx->fb_bufs[i];
            }
            if (ctx->fb_bufs[i] == NULL) {
                ESP_LOGE(TAG, "RAW: restore alloc[%u] failed", i);
                return DAL_ERR_NO_MEM;
            }

            if (ioctl(ctx->fd, VIDIOC_QBUF, v4l2_buf) != 0) {
                ESP_LOGE(TAG, "RAW: restore QBUF[%u] failed", i);
                return DAL_ERR_HW;
            }
        }
    }

    /* ---- 重新启动流 ---- */
    {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(ctx->fd, VIDIOC_STREAMON, &type) != 0) {
            ESP_LOGE(TAG, "RAW: restore STREAMON failed, errno=%d", errno);
            return DAL_ERR_HW;
        }
    }

    ESP_LOGI(TAG, "RAW: capture done, stream restored");
    return DAL_OK;
}

/* ================================================================
 *  BLC 暗帧标定（设备端 Bayer 通道分析）
 * ================================================================ */

/**
 * @brief 按 RGGB Bayer 排列分离四通道并计算均值
 *
 * Bayer 阵列（RGGB）：
 *   (0,0)=R   (0,1)=Gr   (0,2)=R   ...
 *   (1,0)=Gb  (1,1)=B    (1,2)=Gb  ...
 *   ...
 */
static void bayer_rggb_analyze(const uint8_t *raw, int w, int h,
                                float *r_mean, float *gr_mean,
                                float *gb_mean, float *b_mean)
{
    uint64_t sum_r  = 0;
    uint64_t sum_gr = 0;
    uint64_t sum_gb = 0;
    uint64_t sum_b  = 0;
    uint32_t cnt_r  = 0;
    uint32_t cnt_gr = 0;
    uint32_t cnt_gb = 0;
    uint32_t cnt_b  = 0;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t val = raw[y * w + x];
            if ((y & 1) == 0) {
                /* 偶数行: R, Gr, R, Gr, ... */
                if ((x & 1) == 0) {
                    sum_r += val;
                    cnt_r++;
                } else {
                    sum_gr += val;
                    cnt_gr++;
                }
            } else {
                /* 奇数行: Gb, B, Gb, B, ... */
                if ((x & 1) == 0) {
                    sum_gb += val;
                    cnt_gb++;
                } else {
                    sum_b += val;
                    cnt_b++;
                }
            }
        }
    }

    *r_mean  = cnt_r  ? (float)((double)sum_r  / cnt_r)  : 0.0f;
    *gr_mean = cnt_gr ? (float)((double)sum_gr / cnt_gr) : 0.0f;
    *gb_mean = cnt_gb ? (float)((double)sum_gb / cnt_gb) : 0.0f;
    *b_mean  = cnt_b  ? (float)((double)sum_b  / cnt_b)  : 0.0f;
}

dal_err_t bsp_camera_ov5647_blc_calibrate(bool apply)
{
    if (!s_ctx.inited) {
        ESP_LOGE(TAG, "BLC cal: camera not initialized");
        return DAL_ERR_STATE;
    }

    /* RAW8: 1 字节/像素 */
    int w = BOARD_CAMERA_FRAME_WIDTH;
    int h = BOARD_CAMERA_FRAME_HEIGHT;
    size_t raw_size = (size_t)w * h;
    uint8_t *raw_buf = malloc(raw_size);
    if (raw_buf == NULL) {
        ESP_LOGE(TAG, "BLC cal: malloc %u bytes failed", (unsigned)raw_size);
        return DAL_ERR_NO_MEM;
    }

    size_t actual_len = 0;
    dal_err_t ret = ov5647_capture_raw_frame(&s_ctx, raw_buf, raw_size, &actual_len);
    if (ret != DAL_OK || actual_len == 0) {
        ESP_LOGE(TAG, "BLC cal: capture_raw failed, ret=%d len=%u",
                 ret, (unsigned)actual_len);
        free(raw_buf);
        return DAL_ERR_HW;
    }

    if (actual_len < raw_size) {
        ESP_LOGW(TAG, "BLC cal: short frame %u < %u, padding zero",
                 (unsigned)actual_len, (unsigned)raw_size);
        memset(raw_buf + actual_len, 0, raw_size - actual_len);
    }

    /* 分离 Bayer 四通道并计算均值 */
    float r_mean, gr_mean, gb_mean, b_mean;
    bayer_rggb_analyze(raw_buf, w, h, &r_mean, &gr_mean, &gb_mean, &b_mean);

    /* 四舍五入取整 */
    bsp_cam_blc_config_t blc_cfg = {
        .enable               = true,
        .stretch_enable       = false,
        .top_left_offset      = (uint16_t)(r_mean  + 0.5f),
        .top_right_offset     = (uint16_t)(gr_mean + 0.5f),
        .bottom_left_offset   = (uint16_t)(gb_mean + 0.5f),
        .bottom_right_offset  = (uint16_t)(b_mean  + 0.5f),
    };

    ESP_LOGI(TAG, "==== BLC 标定结果 (RGGB Bayer, RAW8) ====");
    ESP_LOGI(TAG, "  R  均值=%.2f -> top_left_offset     =%u",
             (double)r_mean,  (unsigned)blc_cfg.top_left_offset);
    ESP_LOGI(TAG, "  Gr 均值=%.2f -> top_right_offset    =%u",
             (double)gr_mean, (unsigned)blc_cfg.top_right_offset);
    ESP_LOGI(TAG, "  Gb 均值=%.2f -> bottom_left_offset  =%u",
             (double)gb_mean, (unsigned)blc_cfg.bottom_left_offset);
    ESP_LOGI(TAG, "  B  均值=%.2f -> bottom_right_offset =%u",
             (double)b_mean,  (unsigned)blc_cfg.bottom_right_offset);
    ESP_LOGI(TAG, "==== JSON 配置片段 (填入 acc.blc.blc_table) ====");
    ESP_LOGI(TAG, "  \"blc_top_left\": %u, \"blc_top_right\": %u,"
             " \"blc_bottom_left\": %u, \"blc_bottom_right\": %u",
             (unsigned)blc_cfg.top_left_offset,
             (unsigned)blc_cfg.top_right_offset,
             (unsigned)blc_cfg.bottom_left_offset,
             (unsigned)blc_cfg.bottom_right_offset);

    free(raw_buf);

    if (apply) {
        dal_err_t blc_ret = bsp_camera_ov5647_set_blc(&blc_cfg);
        if (blc_ret != DAL_OK) {
            ESP_LOGE(TAG, "BLC cal: apply BLC failed, ret=%d", blc_ret);
            return blc_ret;
        }
        ESP_LOGI(TAG, "BLC cal: sensor BLC applied via I2C");
    }

    return DAL_OK;
}

/* ================================================================
 *  RAW Bayer 帧 hex dump（串口输出，供 PC 端 Python 分析）
 * ================================================================ */
dal_err_t bsp_camera_ov5647_raw_dump(void)
{
    if (!s_ctx.inited) {
        ESP_LOGE(TAG, "RAW dump: camera not initialized");
        return DAL_ERR_STATE;
    }

    int w = BOARD_CAMERA_FRAME_WIDTH;
    int h = BOARD_CAMERA_FRAME_HEIGHT;
    size_t raw_size = (size_t)w * h;
    uint8_t *raw_buf = malloc(raw_size);
    if (raw_buf == NULL) {
        ESP_LOGE(TAG, "RAW dump: malloc %u bytes failed", (unsigned)raw_size);
        return DAL_ERR_NO_MEM;
    }

    size_t actual_len = 0;
    dal_err_t ret = ov5647_capture_raw_frame(&s_ctx, raw_buf, raw_size, &actual_len);
    if (ret != DAL_OK || actual_len == 0) {
        ESP_LOGE(TAG, "RAW dump: capture_raw failed, ret=%d", ret);
        free(raw_buf);
        return DAL_ERR_HW;
    }

    /* ---- 帧头 ---- */
    ESP_LOGI(TAG, "=== RAW_START %d %d RGGB ===", w, h);

    /* hex dump，每行 RAW_DUMP_BYTES_PER_LINE 字节 */
    char line[RAW_DUMP_BYTES_PER_LINE * 2 + 1];
    for (size_t offset = 0; offset < actual_len; offset += RAW_DUMP_BYTES_PER_LINE) {
        size_t chunk = actual_len - offset;
        if (chunk > RAW_DUMP_BYTES_PER_LINE) {
            chunk = RAW_DUMP_BYTES_PER_LINE;
        }
        for (size_t i = 0; i < chunk; i++) {
            static const char hex[] = "0123456789abcdef";
            uint8_t b = raw_buf[offset + i];
            line[i * 2]     = hex[b >> 4];
            line[i * 2 + 1] = hex[b & 0x0f];
        }
        line[chunk * 2] = '\0';
        /* 使用 ESP_LOGI 逐行输出；注意串口速率限制（115200bps ~= 11KB/s） */
        ESP_LOGI(TAG, "%s", line);
    }

    /* ---- 帧尾 ---- */
    ESP_LOGI(TAG, "=== RAW_END %u ===", (unsigned)actual_len);

    free(raw_buf);
    return DAL_OK;
}
