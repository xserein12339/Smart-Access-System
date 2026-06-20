/**
 * @file    pal_i2s.c
 * @brief   PAL I2S 模块 - 实现（ESP-IDF i2s_std 新版 channel API 封装）
 *
 * 内部按方向创建 tx/rx 通道句柄，初始化为标准 Philips 模式。
 * 收发 API 根据方向路由到对应通道。
 */

#include "pal_i2s.h"

#include "driver/i2s_std.h"
#include "driver/i2s_types.h"
#include "esp_err.h"
#include "hal/gpio_types.h"

#include <stdlib.h>

/* ================================================================
 *  内部结构体
 * ================================================================ */

/** @brief PAL I2S 内部状态 */
typedef struct {
    i2s_chan_handle_t tx;   /**< 发送通道句柄（无 TX 方向时为 NULL） */
    i2s_chan_handle_t rx;   /**< 接收通道句柄（无 RX 方向时为 NULL） */
} pal_i2s_internal_t;

/* ================================================================
 *  内部映射
 * ================================================================ */

static i2s_role_t pal_to_esp_role(pal_i2s_role_t role)
{
    return (role == PAL_I2S_ROLE_MASTER) ? I2S_ROLE_MASTER : I2S_ROLE_SLAVE;
}

static i2s_slot_mode_t pal_to_esp_slot_mode(pal_i2s_slot_mode_t m)
{
    return (m == PAL_I2S_SLOT_MONO) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
}

/* ================================================================
 *  生命周期 API
 * ================================================================ */

int pal_i2s_init(pal_i2s_handle_t *handle, const pal_i2s_config_t *cfg)
{
    if (handle == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pal_i2s_internal_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* ---- 1. 分配通道 ---- */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(cfg->port, pal_to_esp_role(cfg->role));
    if (cfg->dma_desc_num > 0) {
        chan_cfg.dma_desc_num = cfg->dma_desc_num;
    }
    if (cfg->dma_frame_num > 0) {
        chan_cfg.dma_frame_num = cfg->dma_frame_num;
    }
    if (cfg->intr_priority > 0) {
        chan_cfg.intr_priority = cfg->intr_priority;
    }

    i2s_chan_handle_t *tx_p = (cfg->dir == PAL_I2S_DIR_TX || cfg->dir == PAL_I2S_DIR_TX_RX) ? &ctx->tx : NULL;
    i2s_chan_handle_t *rx_p = (cfg->dir == PAL_I2S_DIR_RX || cfg->dir == PAL_I2S_DIR_TX_RX) ? &ctx->rx : NULL;

    esp_err_t ret = i2s_new_channel(&chan_cfg, tx_p, rx_p);
    if (ret != ESP_OK) {
        free(ctx);
        return ret;
    }

    /* ---- 2. 初始化为标准 Philips 模式（TX/RX 共用同一套 std 配置） ---- */
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(cfg->sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG((int)cfg->data_bit_width,
                                                        pal_to_esp_slot_mode(cfg->slot_mode)),
        .gpio_cfg = {
            .mclk = (cfg->mclk_pin < 0) ? GPIO_NUM_NC : (gpio_num_t)cfg->mclk_pin,
            .bclk = (cfg->bclk_pin < 0) ? GPIO_NUM_NC : (gpio_num_t)cfg->bclk_pin,
            .ws   = (cfg->ws_pin   < 0) ? GPIO_NUM_NC : (gpio_num_t)cfg->ws_pin,
            .dout = (cfg->dout_pin < 0) ? GPIO_NUM_NC : (gpio_num_t)cfg->dout_pin,
            .din  = (cfg->din_pin  < 0) ? GPIO_NUM_NC : (gpio_num_t)cfg->din_pin,
            .invert_flags = { 0 },
        },
    };

    if (ctx->tx != NULL) {
        ret = i2s_channel_init_std_mode(ctx->tx, &std_cfg);
        if (ret != ESP_OK) {
            goto err_del;
        }
    }
    if (ctx->rx != NULL) {
        ret = i2s_channel_init_std_mode(ctx->rx, &std_cfg);
        if (ret != ESP_OK) {
            goto err_del;
        }
    }

    *handle = (pal_i2s_handle_t)ctx;
    return ESP_OK;

err_del:
    if (ctx->tx != NULL) {
        i2s_del_channel(ctx->tx);
    }
    if (ctx->rx != NULL) {
        i2s_del_channel(ctx->rx);
    }
    free(ctx);
    return ret;
}

int pal_i2s_deinit(pal_i2s_handle_t handle)
{
    pal_i2s_internal_t *ctx = (pal_i2s_internal_t *)handle;
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret_tx = ESP_OK;
    esp_err_t ret_rx = ESP_OK;
    if (ctx->tx != NULL) {
        i2s_channel_disable(ctx->tx);
        ret_tx = i2s_del_channel(ctx->tx);
        ctx->tx = NULL;
    }
    if (ctx->rx != NULL) {
        i2s_channel_disable(ctx->rx);
        ret_rx = i2s_del_channel(ctx->rx);
        ctx->rx = NULL;
    }
    free(ctx);
    return (ret_tx != ESP_OK) ? ret_tx : ret_rx;
}

/* ================================================================
 *  通道控制 API
 * ================================================================ */

int pal_i2s_enable(pal_i2s_handle_t handle)
{
    pal_i2s_internal_t *ctx = (pal_i2s_internal_t *)handle;
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ctx->tx != NULL) {
        esp_err_t ret = i2s_channel_enable(ctx->tx);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (ctx->rx != NULL) {
        esp_err_t ret = i2s_channel_enable(ctx->rx);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

int pal_i2s_disable(pal_i2s_handle_t handle)
{
    pal_i2s_internal_t *ctx = (pal_i2s_internal_t *)handle;
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ctx->tx != NULL) {
        i2s_channel_disable(ctx->tx);
    }
    if (ctx->rx != NULL) {
        i2s_channel_disable(ctx->rx);
    }
    return ESP_OK;
}

/* ================================================================
 *  数据收发 API
 * ================================================================ */

int pal_i2s_write(pal_i2s_handle_t handle, const void *src, size_t len,
                  uint32_t timeout_ms, size_t *bytes_written)
{
    pal_i2s_internal_t *ctx = (pal_i2s_internal_t *)handle;
    if (ctx == NULL || ctx->tx == NULL || src == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t wr = 0;
    esp_err_t ret = i2s_channel_write(ctx->tx, src, len, &wr, timeout_ms);
    if (bytes_written != NULL) {
        *bytes_written = wr;
    }
    return ret;
}

int pal_i2s_read(pal_i2s_handle_t handle, void *dst, size_t len,
                 uint32_t timeout_ms, size_t *bytes_read)
{
    pal_i2s_internal_t *ctx = (pal_i2s_internal_t *)handle;
    if (ctx == NULL || ctx->rx == NULL || dst == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t rd = 0;
    esp_err_t ret = i2s_channel_read(ctx->rx, dst, len, &rd, timeout_ms);
    if (bytes_read != NULL) {
        *bytes_read = rd;
    }
    return ret;
}
