/**
 * @file    dal_audio_interface.h
 * @brief   音频设备 DAL 接口契约（纯业务语义，无硬件依赖）
 *
 * @details 此文件是音频模块的纯业务语义接口规范。
 *          - Service 层、Mock 测试、BSP 适配层均应仅包含此文件。
 *          - 严禁包含任何平台头文件（如 pal_i2s.h、driver/i2s.h）或
 *            DAL 管理头（dal_audio.h）。
 *          - 音频数据为原始 PCM（int16_t 交错的左右声道），Service 直接
 *            读写，底层 codec/I2S 细节由 BSP 封装。
 *          - 所有硬件上下文封装在不透明指针 void *ctx 中。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef DAL_AUDIO_INTERFACE_H
#define DAL_AUDIO_INTERFACE_H

#include "dal_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 音频配置（纯业务语义） */
typedef struct {
    uint32_t sample_rate_hz;   /**< 采样率（如 16000、44100） */
    uint8_t  volume;           /**< 音量百分比 0~100（BSP 映射到 codec 寄存器） */
} dal_audio_config_t;

/** 音频操作契约 */
typedef struct {
    /**
     * @brief 初始化音频设备（codec + I2S），应用采样率与音量
     * @param[in] ctx 驱动上下文
     * @param[in] cfg 配置
     * @return DAL_OK 成功
     */
    dal_err_t (*init)(void *ctx, const dal_audio_config_t *cfg);

    /**
     * @brief 播放 PCM 数据（阻塞至写入完成或超时）
     * @param[in] ctx  驱动上下文
     * @param[in] data PCM 数据（int16_t 交错立体声）
     * @param[in] len  数据字节数
     * @return DAL_OK 成功，DAL_ERR_TIMEOUT 写入超时
     */
    dal_err_t (*play)(void *ctx, const void *data, size_t len);

    /**
     * @brief 录制 PCM 数据（阻塞至读满或超时）
     * @param[in]  ctx  驱动上下文
     * @param[out] data PCM 数据缓冲
     * @param[in]  len  期望字节数
     * @return DAL_OK 成功，DAL_ERR_TIMEOUT 读取超时
     */
    dal_err_t (*record)(void *ctx, void *data, size_t len);

    /**
     * @brief 设置音量
     * @param[in] ctx     驱动上下文
     * @param[in] percent 音量百分比 0~100
     * @return DAL_OK 成功
     */
    dal_err_t (*set_volume)(void *ctx, uint8_t percent);

    /**
     * @brief 静音/取消静音
     * @param[in] ctx  驱动上下文
     * @param[in] mute true=静音, false=取消
     * @return DAL_OK 成功
     */
    dal_err_t (*set_mute)(void *ctx, bool mute);

    /**
     * @brief 查询是否正在播放/录制
     * @param[in] ctx 驱动上下文
     * @return true 忙，false 空闲
     */
    bool (*is_busy)(void *ctx);

    /** @brief 反初始化 */
    dal_err_t (*deinit)(void *ctx);

    void *ctx;              /**< BSP 私有上下文，由 create() 注入 */
} dal_audio_ops_t;

#ifdef __cplusplus
}
#endif
#endif /* DAL_AUDIO_INTERFACE_H */
