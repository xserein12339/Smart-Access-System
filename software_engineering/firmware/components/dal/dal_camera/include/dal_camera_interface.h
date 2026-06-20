/**
 * @file    dal_camera_interface.h
 * @brief   摄像头设备 DAL 接口契约（纯业务语义，无硬件依赖）
 *
 * @details 此文件是摄像头模块的纯业务语义接口规范。
 *          - Service 层、Mock 测试、BSP 适配层均应仅包含此文件。
 *          - 严禁包含任何平台头文件（如 esp_video、pal_cam.h）或
 *            DAL 管理头（dal_camera.h）。
 *          - 采用回调式帧推送模型：start_stream 注册帧回调，BSP 内部
 *            采集任务循环取帧并经回调投递给 Service；Service 处理完
 *            一帧后调 return_frame 归还缓冲。
 *          - 所有硬件上下文封装在不透明指针 void *ctx 中。
 *
 * @author  xLumina
 * @version 1.0
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

/**
 * @brief 帧就绪回调（BSP 采集任务调用，运行在 BSP 采集任务上下文）
 *
 * @param[in] user_data start_stream 注册时传入的用户数据
 * @param[in] frame     帧描述指针（仅在本回调内有效，处理完须 return_frame）
 *
 * @note 回调内应尽快处理或拷贝数据，避免长时间持有帧缓冲阻塞采集。
 *       禁止在回调内阻塞或调用 return_frame 以外的耗时 DAL API。
 */
typedef void (*dal_camera_frame_cb_t)(void *user_data, const dal_camera_frame_t *frame);

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
     * @brief 启动帧流推送
     * @param[in] ctx       驱动上下文
     * @param[in] cb        帧就绪回调
     * @param[in] user_data 传给回调的用户数据
     * @return DAL_OK 成功，DAL_ERR_STATE 已在推流或未初始化
     */
    dal_err_t (*start_stream)(void *ctx, dal_camera_frame_cb_t cb, void *user_data);

    /**
     * @brief 停止帧流推送
     * @return DAL_OK 成功
     */
    dal_err_t (*stop_stream)(void *ctx);

    /**
     * @brief 归还帧缓冲（回调处理完后调用）
     * @param[in] ctx   驱动上下文
     * @param[in] frame 帧描述（frame_handle 用于 BSP 内部归还）
     * @return DAL_OK 成功
     */
    dal_err_t (*return_frame)(void *ctx, const dal_camera_frame_t *frame);

    /**
     * @brief 反初始化
     */
    dal_err_t (*deinit)(void *ctx);
} dal_camera_ops_t;

#ifdef __cplusplus
}
#endif
#endif /* DAL_CAMERA_INTERFACE_H */
