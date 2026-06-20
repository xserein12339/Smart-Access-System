/**
 * @file    pal_uart.h
 * @brief   PAL UART 模块 — 异步串行通信
 *
 * 封装 ESP-IDF driver/uart.h，提供阻塞发送 / 超时接收 API。
 *
 * 参考文档：ESP32-P4 TRM UART 章节
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef PAL_UART_H
#define PAL_UART_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief UART 不透明句柄 */
typedef struct pal_uart_internal *pal_uart_handle_t;

/* ================================================================
 *  枚举类型
 * ================================================================ */

/** @brief 数据位宽 */
typedef enum {
    PAL_UART_DATA_5 = 0,   /**< 5 位 */
    PAL_UART_DATA_6 = 1,   /**< 6 位 */
    PAL_UART_DATA_7 = 2,   /**< 7 位 */
    PAL_UART_DATA_8 = 3,   /**< 8 位 */
} pal_uart_data_bits_t;

/** @brief 停止位 */
typedef enum {
    PAL_UART_STOP_1   = 1,   /**< 1 位停止位 */
    PAL_UART_STOP_1_5 = 2,   /**< 1.5 位停止位 */
    PAL_UART_STOP_2   = 3,   /**< 2 位停止位 */
} pal_uart_stop_bits_t;

/** @brief 校验位 */
typedef enum {
    PAL_UART_PARITY_NONE = 0,   /**< 无校验 */
    PAL_UART_PARITY_EVEN = 2,   /**< 偶校验 */
    PAL_UART_PARITY_ODD  = 3,   /**< 奇校验 */
} pal_uart_parity_t;

/** @brief 硬件流控 */
typedef enum {
    PAL_UART_HW_FLOWCTRL_DISABLE  = 0,   /**< 关闭流控 */
    PAL_UART_HW_FLOWCTRL_RTS      = 1,   /**< 仅 RTS */
    PAL_UART_HW_FLOWCTRL_CTS      = 2,   /**< 仅 CTS */
    PAL_UART_HW_FLOWCTRL_CTS_RTS  = 3,   /**< CTS + RTS */
} pal_uart_hw_flowctrl_t;

/* ================================================================
 *  配置结构体
 * ================================================================ */

/**
 * @brief UART 初始化配置
 */
typedef struct {
    int                    port_num;      /**< UART 端口号（0, 1, 2, …） */
    int                    tx_pin;        /**< TX 引脚，-1 表示不使用 */
    int                    rx_pin;        /**< RX 引脚，-1 表示不使用 */
    int                    rts_pin;       /**< RTS 引脚，-1 表示不使用 */
    int                    cts_pin;       /**< CTS 引脚，-1 表示不使用 */
    int                    baud_rate;     /**< 波特率（如 115200, 921600） */
    pal_uart_data_bits_t   data_bits;     /**< 数据位宽 */
    pal_uart_stop_bits_t   stop_bits;     /**< 停止位 */
    pal_uart_parity_t      parity;        /**< 校验模式 */
    pal_uart_hw_flowctrl_t flow_ctrl;     /**< 硬件流控模式 */
    int                    rx_buf_size;   /**< RX 环形缓冲区大小（字节） */
    int                    tx_buf_size;   /**< TX 环形缓冲区大小（字节） */
} pal_uart_config_t;

/* ================================================================
 *  生命周期 API
 * ================================================================ */

/**
 * @brief 初始化 UART 端口并分配缓冲
 *
 * @param[out] handle 返回的不透明句柄
 * @param[in]  cfg    UART 配置
 * @return 0 成功，负数失败
 */
int pal_uart_init(pal_uart_handle_t *handle, const pal_uart_config_t *cfg);

/**
 * @brief 反初始化 UART，清空缓冲，释放资源
 *
 * @param handle UART 句柄
 * @return 0 成功
 */
int pal_uart_deinit(pal_uart_handle_t handle);

/**
 * @brief 运行时动态修改波特率
 *
 * @param handle    UART 句柄
 * @param baud_rate 新波特率
 * @return 0 成功
 */
int pal_uart_set_baudrate(pal_uart_handle_t handle, int baud_rate);

/* ================================================================
 *  数据传输 API
 * ================================================================ */

/**
 * @brief 发送数据（阻塞）
 *
 * @param handle UART 句柄
 * @param data   发送缓冲区指针
 * @param len    发送字节数
 * @return 实际发送字节数，负数表示错误
 */
int pal_uart_send(pal_uart_handle_t handle, const uint8_t *data, size_t len);

/**
 * @brief 接收数据（带超时阻塞）
 *
 * @param handle      UART 句柄
 * @param[out] data   接收缓冲区指针
 * @param len         最大接收字节数
 * @param timeout_ms  超时时间（ms），0 = 非阻塞
 * @return 实际接收字节数，负数表示错误
 */
int pal_uart_recv(pal_uart_handle_t handle, uint8_t *data, size_t len,
                  uint32_t timeout_ms);

/**
 * @brief 等待 TX FIFO 排空
 *
 * @param handle     UART 句柄
 * @param timeout_ms 超时时间（ms）
 * @return 0 成功，负数超时/错误
 */
int pal_uart_flush(pal_uart_handle_t handle, uint32_t timeout_ms);

/**
 * @brief 查询 RX 缓冲区中待读取的字节数
 *
 * @param handle UART 句柄
 * @return 待读取字节数，负数表示错误
 */
int pal_uart_get_rx_buffered_len(pal_uart_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* PAL_UART_H */
