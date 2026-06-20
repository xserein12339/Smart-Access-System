/**
 * @file    pal_i2s.h
 * @brief   PAL I2S 模块 — I2S 标准（Philips）模式音频收发
 *
 * 封装 ESP-IDF driver/i2s_std.h（新版 channel API），提供：
 *   - I2S 通道初始化（主机/从机，TX/RX/双工）
 *   - 使能 / 禁能通道
 *   - 阻塞式发送 / 接收（带超时）
 *
 * 仅支持标准（Philips/MSB/PCM）模式，默认使用 Philips 格式。
 *
 * 参考文档：ESP32-P4 TRM I2S 章节
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef PAL_I2S_H
#define PAL_I2S_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief I2S 通道不透明句柄 */
typedef void *pal_i2s_handle_t;

/* ================================================================
 *  枚举类型
 * ================================================================ */

/** @brief I2S 角色 */
typedef enum {
    PAL_I2S_ROLE_MASTER = 0,   /**< 主机：BCLK/WS 输出 */
    PAL_I2S_ROLE_SLAVE  = 1,   /**< 从机：BCLK/WS 输入 */
} pal_i2s_role_t;

/** @brief 数据方向 */
typedef enum {
    PAL_I2S_DIR_TX      = 0,   /**< 仅发送 */
    PAL_I2S_DIR_RX      = 1,   /**< 仅接收 */
    PAL_I2S_DIR_TX_RX   = 2,   /**< 全双工（同时收发） */
} pal_i2s_dir_t;

/** @brief slot 模式 */
typedef enum {
    PAL_I2S_SLOT_MONO    = 0,   /**< 单声道 */
    PAL_I2S_SLOT_STEREO  = 1,   /**< 立体声 */
} pal_i2s_slot_mode_t;

/** @brief 数据位宽（与采样位宽一致） */
typedef enum {
    PAL_I2S_DATA_8BIT  = 8,    /**< 8 bit */
    PAL_I2S_DATA_16BIT = 16,   /**< 16 bit */
    PAL_I2S_DATA_24BIT = 24,   /**< 24 bit */
    PAL_I2S_DATA_32BIT = 32,   /**< 32 bit */
} pal_i2s_data_bit_width_t;

/* ================================================================
 *  配置结构体
 * ================================================================ */

/**
 * @brief I2S 通道初始化配置
 */
typedef struct {
    int                          port;            /**< I2S 端口号，-1 自动选择 */
    pal_i2s_role_t               role;            /**< 主/从机 */
    pal_i2s_dir_t                dir;             /**< 数据方向 */
    int                          mclk_pin;        /**< MCLK 引脚，-1 不使用 */
    int                          bclk_pin;        /**< BCLK 引脚 */
    int                          ws_pin;          /**< WS（LRCK）引脚 */
    int                          dout_pin;        /**< 串行数据输出引脚，-1 不使用 */
    int                          din_pin;         /**< 串行数据输入引脚，-1 不使用 */
    uint32_t                     sample_rate_hz;  /**< 采样率（Hz），如 16000/44100/48000 */
    pal_i2s_data_bit_width_t     data_bit_width;  /**< 数据位宽 */
    pal_i2s_slot_mode_t          slot_mode;       /**< 单/双声道 */
    int                          dma_desc_num;    /**< DMA 描述符数量（默认 6） */
    int                          dma_frame_num;   /**< 每个 DMA 描述符帧数（默认 240） */
    int                          intr_priority;   /**< 中断优先级，0 默认 */
} pal_i2s_config_t;

/* ================================================================
 *  生命周期 API
 * ================================================================ */

/**
 * @brief 初始化 I2S 通道（标准 Philips 模式）
 *
 * @param[out] handle 返回的通道句柄
 * @param[in]  cfg    配置
 * @return 0 成功，负数失败
 *
 * @note 初始化后通道处于 REGISTERED 态，需调用 pal_i2s_enable() 才能收发。
 */
int pal_i2s_init(pal_i2s_handle_t *handle, const pal_i2s_config_t *cfg);

/**
 * @brief 反初始化 I2S 通道并释放资源
 *
 * @param handle 通道句柄
 * @return 0 成功，负数失败
 */
int pal_i2s_deinit(pal_i2s_handle_t handle);

/* ================================================================
 *  通道控制 API
 * ================================================================ */

/**
 * @brief 使能 I2S 通道（进入 READY/RUNNING 态，开始收发）
 *
 * @param handle 通道句柄
 * @return 0 成功，负数失败
 */
int pal_i2s_enable(pal_i2s_handle_t handle);

/**
 * @brief 禁用 I2S 通道（暂停收发，保留配置）
 *
 * @param handle 通道句柄
 * @return 0 成功，负数失败
 */
int pal_i2s_disable(pal_i2s_handle_t handle);

/* ================================================================
 *  数据收发 API
 * ================================================================ */

/**
 * @brief 发送数据（阻塞，带超时）
 *
 * @param handle        通道句柄
 * @param src           发送缓冲区
 * @param len           发送字节数
 * @param timeout_ms    超时（ms），0 立即返回，UINT32_MAX 永久等待
 * @param[out] bytes_written 实际写入字节数（可为 NULL）
 * @return 0 成功，负数失败/超时
 */
int pal_i2s_write(pal_i2s_handle_t handle, const void *src, size_t len,
                  uint32_t timeout_ms, size_t *bytes_written);

/**
 * @brief 接收数据（阻塞，带超时）
 *
 * @param handle        通道句柄
 * @param[out] dst      接收缓冲区
 * @param len           最大接收字节数
 * @param timeout_ms    超时（ms）
 * @param[out] bytes_read 实际读取字节数（可为 NULL）
 * @return 0 成功，负数失败/超时
 */
int pal_i2s_read(pal_i2s_handle_t handle, void *dst, size_t len,
                 uint32_t timeout_ms, size_t *bytes_read);

#ifdef __cplusplus
}
#endif

#endif /* PAL_I2S_H */
