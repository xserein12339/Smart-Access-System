/**
 * @file    pal_uart.c
 * @brief   PAL UART 模块 - 实现（ESP-IDF uart 驱动封装）
 */

#include "pal_uart.h"

#include "driver/uart.h"
#include "esp_err.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  内部结构体
 * ================================================================ */

/**
 * @brief UART 不透明句柄内部结构
 */
typedef struct pal_uart_internal {
    int  port;    /**< UART 端口号 */
    bool inited;  /**< 是否已成功初始化 */
} pal_uart_internal_t;

/* ================================================================
 *  枚举映射函数
 * ================================================================ */

/** @brief PAL 数据位 → ESP-IDF 枚举 */
static uart_word_length_t pal_to_esp_data_bits(pal_uart_data_bits_t bits)
{
    switch (bits) {
    case PAL_UART_DATA_5: return UART_DATA_5_BITS;
    case PAL_UART_DATA_6: return UART_DATA_6_BITS;
    case PAL_UART_DATA_7: return UART_DATA_7_BITS;
    case PAL_UART_DATA_8: return UART_DATA_8_BITS;
    default:              return UART_DATA_8_BITS;
    }
}

/** @brief PAL 停止位 → ESP-IDF 枚举 */
static uart_stop_bits_t pal_to_esp_stop_bits(pal_uart_stop_bits_t bits)
{
    switch (bits) {
    case PAL_UART_STOP_1:   return UART_STOP_BITS_1;
    case PAL_UART_STOP_1_5: return UART_STOP_BITS_1_5;
    case PAL_UART_STOP_2:   return UART_STOP_BITS_2;
    default:                return UART_STOP_BITS_1;
    }
}

/** @brief PAL 校验 → ESP-IDF 枚举 */
static uart_parity_t pal_to_esp_parity(pal_uart_parity_t parity)
{
    switch (parity) {
    case PAL_UART_PARITY_NONE: return UART_PARITY_DISABLE;
    case PAL_UART_PARITY_EVEN: return UART_PARITY_EVEN;
    case PAL_UART_PARITY_ODD:  return UART_PARITY_ODD;
    default:                   return UART_PARITY_DISABLE;
    }
}

/** @brief PAL 流控 → ESP-IDF 枚举 */
static uart_hw_flowcontrol_t pal_to_esp_flow(pal_uart_hw_flowctrl_t fc)
{
    switch (fc) {
    case PAL_UART_HW_FLOWCTRL_DISABLE:  return UART_HW_FLOWCTRL_DISABLE;
    case PAL_UART_HW_FLOWCTRL_RTS:      return UART_HW_FLOWCTRL_RTS;
    case PAL_UART_HW_FLOWCTRL_CTS:      return UART_HW_FLOWCTRL_CTS;
    case PAL_UART_HW_FLOWCTRL_CTS_RTS:  return UART_HW_FLOWCTRL_CTS_RTS;
    default:                            return UART_HW_FLOWCTRL_DISABLE;
    }
}

/* ================================================================
 *  生命周期
 * ================================================================ */

int pal_uart_init(pal_uart_handle_t *handle, const pal_uart_config_t *cfg)
{
    if (handle == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pal_uart_internal_t *u = calloc(1, sizeof(*u));
    if (u == NULL) {
        return ESP_ERR_NO_MEM;
    }
    u->port   = cfg->port_num;
    u->inited = false;

    /* 构建 ESP-IDF 配置结构体 */
    uart_config_t esp_cfg = {
        .baud_rate  = cfg->baud_rate,
        .data_bits  = pal_to_esp_data_bits(cfg->data_bits),
        .stop_bits  = pal_to_esp_stop_bits(cfg->stop_bits),
        .parity     = pal_to_esp_parity(cfg->parity),
        .flow_ctrl  = pal_to_esp_flow(cfg->flow_ctrl),
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(cfg->port_num, &esp_cfg);
    if (ret != ESP_OK) {
        free(u);
        return ret;
    }

    ret = uart_set_pin(cfg->port_num, cfg->tx_pin, cfg->rx_pin,
                       cfg->rts_pin, cfg->cts_pin);
    if (ret != ESP_OK) {
        free(u);
        return ret;
    }

    ret = uart_driver_install(cfg->port_num, cfg->rx_buf_size,
                              cfg->tx_buf_size, 0, NULL, 0);
    if (ret != ESP_OK) {
        free(u);
        return ret;
    }

    u->inited = true;
    *handle = (pal_uart_handle_t)u;
    return ESP_OK;
}

int pal_uart_deinit(pal_uart_handle_t handle)
{
    pal_uart_internal_t *u = (pal_uart_internal_t *)handle;
    if (u == NULL || !u->inited) {
        return ESP_ERR_INVALID_ARG;
    }
    uart_driver_delete(u->port);
    free(u);
    return ESP_OK;
}

int pal_uart_set_baudrate(pal_uart_handle_t handle, int baud_rate)
{
    pal_uart_internal_t *u = (pal_uart_internal_t *)handle;
    if (u == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return uart_set_baudrate(u->port, baud_rate);
}

/* ================================================================
 *  数据传输
 * ================================================================ */

int pal_uart_send(pal_uart_handle_t handle, const uint8_t *data, size_t len)
{
    pal_uart_internal_t *u = (pal_uart_internal_t *)handle;
    if (u == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return uart_write_bytes(u->port, data, len);
}

int pal_uart_recv(pal_uart_handle_t handle, uint8_t *data, size_t len,
                  uint32_t timeout_ms)
{
    pal_uart_internal_t *u = (pal_uart_internal_t *)handle;
    if (u == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return uart_read_bytes(u->port, data, len, pdMS_TO_TICKS(timeout_ms));
}

int pal_uart_flush(pal_uart_handle_t handle, uint32_t timeout_ms)
{
    pal_uart_internal_t *u = (pal_uart_internal_t *)handle;
    if (u == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return uart_wait_tx_done(u->port, pdMS_TO_TICKS(timeout_ms));
}

int pal_uart_get_rx_buffered_len(pal_uart_handle_t handle)
{
    pal_uart_internal_t *u = (pal_uart_internal_t *)handle;
    if (u == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t len = 0;
    esp_err_t ret = uart_get_buffered_data_len(u->port, &len);
    return (ret == ESP_OK) ? (int)len : (int)ret;
}
