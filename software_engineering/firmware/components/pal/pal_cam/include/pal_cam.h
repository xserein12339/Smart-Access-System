/**
 * @file    pal_cam.h
 * @brief   PAL CAM 模块 — MIPI-CSI 摄像头采集（esp_video V4L2 路径）
 *
 * 封装 ESP-IDF esp_video 框架（V4L2 风格 ioctl），提供：
 *   - 摄像头初始化（sensor 自动识别 + CSI + ISP，复用 PAL I2C SCCB 总线）
 *   - 流式采集（VIDIOC_STREAMON）
 *   - 取帧 / 还帧（DQBUF / QBUF，零拷贝，帧缓冲由 PAL 管理）
 *
 * 典型用法：
 *   pal_cam_init(&h, &cfg);
 *   for (;;) {
 *       pal_cam_frame_t fb;
 *       pal_cam_get_frame(h, &fb);
 *       // 处理 fb.buf（fb.len 字节）…
 *       pal_cam_return_frame(h, &fb);
 *   }
 *   pal_cam_deinit(h);
 *
 * 参考文档：ESP32-P4 TRM MIPI-CSI 章节 / ESP-IDF esp_video 文档
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef PAL_CAM_H
#define PAL_CAM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/time.h>

#include "pal_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 摄像头句柄 */
typedef void *pal_cam_handle_t;

/* ================================================================
 *  枚举类型
 * ================================================================ */

/** @brief 像素格式 */
typedef enum {
    PAL_CAM_FMT_RGB565 = 0,   /**< RGB565，2 字节/像素，适合直送 LCD */
    PAL_CAM_FMT_RGB888 = 1,   /**< RGB888，3 字节/像素 */
    PAL_CAM_FMT_JPEG   = 2,   /**< JPEG 压缩流 */
} pal_cam_fmt_t;

/** @brief 帧缓冲内存类型 */
typedef enum {
    PAL_CAM_MEM_MMAP    = 0,   /**< 内核映射内存（驱动分配） */
    PAL_CAM_MEM_USERPTR = 1,   /**< 用户指针（PSRAM 分配，适合大缓冲） */
} pal_cam_buf_mem_t;

/* ================================================================
 *  配置与帧结构体
 * ================================================================ */

/**
 * @brief 摄像头初始化配置
 */
typedef struct {
    pal_i2c_bus_handle_t i2c_bus;   /**< PAL I2C 总线句柄（用于 SCCB 传感器配置） */
    int                  reset_pin; /**< 传感器复位引脚，-1 不使用 */
    int                  pwdn_pin;  /**< 传感器掉电引脚，-1 不使用 */
    bool                 dont_init_ldo; /**< true=外部(PAL)已配 CSI LDO；false=esp_video 自配 */
    pal_cam_fmt_t        format;    /**< 期望像素格式 */
    uint8_t              fb_count;  /**< 帧缓冲数量，≥2 */
    pal_cam_buf_mem_t    mem_type;  /**< 帧缓冲内存类型 */
    bool                 vflip;     /**< 垂直翻转 */
    bool                 hflip;     /**< 水平翻转 */
} pal_cam_config_t;

/**
 * @brief 一帧图像描述（零拷贝，buf 指向内部帧缓冲）
 */
typedef struct {
    void          *buf;      /**< 帧数据指针（不要释放） */
    size_t         len;      /**< 有效数据字节数 */
    uint16_t       width;    /**< 帧宽（像素） */
    uint16_t       height;   /**< 帧高（像素） */
    pal_cam_fmt_t  format;   /**< 像素格式 */
    struct timeval timestamp;/**< 采集时间戳 */
    void          *_priv;    /**< 内部使用，调用方勿修改 */
} pal_cam_frame_t;

/* ================================================================
 *  生命周期 API
 * ================================================================ */

/**
 * @brief 初始化摄像头并启动采集流
 *
 * 内部流程：esp_video_init → open(/dev/video0) → 设置翻转/格式 → 申请帧缓冲 → STREAMON
 *
 * @param[out] handle 返回的句柄
 * @param[in]  cfg    配置
 * @return 0 成功，负数失败
 */
int pal_cam_init(pal_cam_handle_t *handle, const pal_cam_config_t *cfg);

/**
 * @brief 停止采集并释放摄像头资源
 *
 * @param handle 句柄
 * @return 0 成功，负数失败
 */
int pal_cam_deinit(pal_cam_handle_t handle);

/* ================================================================
 *  帧采集 API
 * ================================================================ */

/**
 * @brief 取出一帧（阻塞，直到有帧可用）
 *
 * @param handle     句柄
 * @param[out] frame 帧描述（buf 指向内部缓冲，用完需 pal_cam_return_frame 归还）
 * @return 0 成功，负数失败
 */
int pal_cam_get_frame(pal_cam_handle_t handle, pal_cam_frame_t *frame);

/**
 * @brief 归还帧缓冲（归还后才可再次被驱动填充）
 *
 * @param handle 句柄
 * @param frame  pal_cam_get_frame 返回的帧描述
 * @return 0 成功，负数失败
 */
int pal_cam_return_frame(pal_cam_handle_t handle, pal_cam_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* PAL_CAM_H */
