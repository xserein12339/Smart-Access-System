/**
 * @file    dal_camera_interface.h
 * @brief   摄像头设备 DAL 接口契约（纯业务语义，无硬件依赖）
 *
 * @details 此文件是摄像头模块的纯业务语义接口规范。
 *          - Service 层、Mock 测试、BSP 适配层均应仅包含此文件。
 *          - 严禁包含任何平台头文件（如 esp_video、pal_cam.h）或
 *            DAL 管理头（dal_camera.h）。
 *          - 采用主动 capture 式：BSP 仅提供同步 capture_frame() 取帧原语，
 *            不创建采集任务；采集任务由 service/main 层运行，循环调
 *            capture_frame 取帧、return_frame 归还。BSP/DAL 不维护 OS 任务。
 *          - 所有硬件上下文封装在不透明指针 void *ctx 中。
 *
 * @author  xiamu
 * @version 1.1
 */
#ifndef DAL_CAMERA_INTERFACE_H
#define DAL_CAMERA_INTERFACE_H

#include "dal_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 摄像头像素格式（纯业务枚举） */
typedef enum {
    DAL_CAMERA_FMT_RGB565 = 0,   /**< RGB565 */
    DAL_CAMERA_FMT_RGB888 = 1,   /**< RGB888 */
    DAL_CAMERA_FMT_JPEG   = 2,   /**< JPEG */
} dal_camera_fmt_t;

/** 摄像头配置（纯业务语义） */
typedef struct {
    uint16_t           width;     /**< 期望帧宽 */
    uint16_t           height;    /**< 期望帧高 */
    dal_camera_fmt_t   format;    /**< 像素格式 */
    uint8_t            fb_count;  /**< 帧缓冲数量（>=2） */
    bool               hflip;     /**< 水平翻转 */
    bool               vflip;     /**< 垂直翻转 */
} dal_camera_config_t;

/** 一帧图像描述（纯业务，不含 PAL 类型） */
typedef struct {
    void             *buf;          /**< 帧数据指针（BSP 提供，生命周期至 return_frame） */
    size_t            len;          /**< 帧数据字节长度 */
    uint16_t          width;        /**< 实际帧宽 */
    uint16_t          height;       /**< 实际帧高 */
    dal_camera_fmt_t  format;       /**< 实际像素格式 */
    uint64_t          timestamp_us; /**< 时间戳（us，自启动） */
    void             *frame_handle; /**< BSP 内部帧句柄（return_frame 时回传） */
} dal_camera_frame_t;

/** 摄像头操作契约 */
typedef struct {
    /**
     * @brief 初始化摄像头（应用运行参数）
     * @param[in] ctx 驱动上下文
     * @param[in] cfg 配置
     * @return DAL_OK 成功
     */
    dal_err_t (*init)(void *ctx, const dal_camera_config_t *cfg);

    /**
     * @brief 同步取一帧图像（阻塞）
     *
     * @param[in]  ctx        驱动上下文
     * @param[in]  timeout_ms 超时毫秒数，-1 永久阻塞；超时返回 DAL_ERR_TIMEOUT
     * @param[out] frame      帧描述（buf/len/width/height/format/timestamp/frame_handle）
     * @return DAL_OK 成功，DAL_ERR_TIMEOUT 超时无帧，其他见 dal_err_t
     *
     * @note 本函数阻塞至有帧就绪或超时。BSP 不创建任务，由调用方在自有
     *       任务上下文循环调用。frame->buf 仅在 return_frame 调用前有效。
     * @warning 必须配对调用 return_frame 归还帧缓冲，否则 V4L2 队列耗尽。
     */
    dal_err_t (*capture_frame)(void *ctx, int timeout_ms, dal_camera_frame_t *frame);

    /**
     * @brief 归还帧缓冲（capture_frame 处理完后调用）
     * @param[in] ctx   驱动上下文
     * @param[in] frame 帧描述（frame_handle 用于 BSP 内部归还）
     * @return DAL_OK 成功
     */
    dal_err_t (*return_frame)(void *ctx, const dal_camera_frame_t *frame);

    /**
     * @brief 反初始化
     */
    dal_err_t (*deinit)(void *ctx);

    void *ctx;              /**< BSP 私有上下文，由 create() 注入 */
} dal_camera_ops_t;

#ifdef __cplusplus
}
#endif
#endif /* DAL_CAMERA_INTERFACE_H */
