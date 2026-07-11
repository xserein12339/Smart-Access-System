/**
 * @file    bsp_audio_es8311_codec.h
 * @brief   ES8311 音频 CODEC 子驱动（BSP 内部，不放入 include/）
 *
 * @details ES8311 经 I2C（地址 0x18）配置寄存器，配合 I2S 完成 DAC/ADC。
 *          本驱动封装寄存器初始化序列、音量、静音控制。
 *          仅 bsp_audio_es8311.c 可包含本文件。
 *
 *          直接调用 ESP-IDF driver/i2c_master，bus 参数为
 *          i2c_master_bus_handle_t（由 board_i2c_get_bus() 转型得到）。
 *
 *          参考：ES8311 Datasheet
 *
 * @author  xLumina
 * @version 1.1
 */
#ifndef BSP_AUDIO_ES8311_CODEC_H
#define BSP_AUDIO_ES8311_CODEC_H

#include "dal_err.h"
#include "driver/i2c_master.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ES8311 寄存器地址 ---- */
#define ES8311_REG_RESET      0x00   /**< 复位 / 模式控制 */
#define ES8311_REG_CLK_MGR1   0x01   /**< 时钟管理1 */
#define ES8311_REG_CLK_MGR2   0x02
#define ES8311_REG_CLK_MGR3   0x03
#define ES8311_REG_CLK_MGR4   0x04
#define ES8311_REG_CLK_MGR5   0x05
#define ES8311_REG_CLK_MGR6   0x06   /**< BCLK 分频 */
#define ES8311_REG_CLK_MGR7   0x07
#define ES8311_REG_CLK_MGR8   0x08
#define ES8311_REG_SDP_IN     0x09   /**< DAC 串行输入端口 */
#define ES8311_REG_SDP_OUT    0x0A   /**< ADC 串行输出端口 */
#define ES8311_REG_SYS0B      0x0B
#define ES8311_REG_SYS0C      0x0C
#define ES8311_REG_SYS0D      0x0D   /**< 电源控制 */
#define ES8311_REG_SYS0E      0x0E
#define ES8311_REG_SYS10      0x10
#define ES8311_REG_SYS11      0x11
#define ES8311_REG_SYS12      0x12   /**< DAC 使能 */
#define ES8311_REG_SYS13      0x13
#define ES8311_REG_SYS14      0x14
#define ES8311_REG_ADC15      0x15
#define ES8311_REG_ADC16      0x16
#define ES8311_REG_ADC17      0x17
#define ES8311_REG_ADC1B      0x1B
#define ES8311_REG_ADC1C      0x1C
#define ES8311_REG_DAC31      0x31   /**< DAC 静音控制 */
#define ES8311_REG_DAC32      0x32   /**< DAC 音量 */
#define ES8311_REG_DAC37      0x37
#define ES8311_REG_GPIO44     0x44
#define ES8311_REG_GP45       0x45

#define ES8311_REG_VOLUME     ES8311_REG_DAC32

/** 音量预设 */
#define ES8311_VOLUME_MIN     0x00   /**< 静音 */
#define ES8311_VOLUME_MAX     0xC0   /**< 0dB 最大 */

/** ES8311 驱动上下文（BSP 私有） */
typedef struct {
    i2c_master_dev_handle_t dev;   /**< I2C 设备句柄（共享总线挂载） */
    bool                    inited;
} bsp_es8311_ctx_t;

/**
 * @brief 初始化 ES8311（复位 + I2S slave 模式 + 16-bit Philips + 启用 DAC）
 * @param[out] ctx 上下文
 * @param[in]  bus 共享 I2C 总线句柄（i2c_master_bus_handle_t）
 * @return DAL_OK 成功
 * @note I2C 地址取自 board_v1_config.h 的 BOARD_AUDIO_CODEC_I2C_ADDR。
 */
dal_err_t bsp_es8311_init(bsp_es8311_ctx_t *ctx, i2c_master_bus_handle_t bus);

/** @brief 反初始化 */
dal_err_t bsp_es8311_deinit(bsp_es8311_ctx_t *ctx);

/**
 * @brief 设置音量
 * @param[in] percent 音量百分比 0~100（映射到 0x00~0xC0）
 */
dal_err_t bsp_es8311_set_volume(bsp_es8311_ctx_t *ctx, uint8_t percent);

/** @brief 静音/取消静音 */
dal_err_t bsp_es8311_set_mute(bsp_es8311_ctx_t *ctx, bool mute);

#ifdef __cplusplus
}
#endif
#endif /* BSP_AUDIO_ES8311_CODEC_H */
