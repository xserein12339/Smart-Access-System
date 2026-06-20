/**
 * @file    pal_ldo.c
 * @brief   PAL LDO 模块 - 实现（ESP-IDF esp_ldo_regulator 封装）
 *
 * 内部仅持有 esp_ldo_channel_handle_t，所有 API 直接映射到底层驱动。
 */

#include "pal_ldo.h"

#include "esp_ldo_regulator.h"
#include "esp_err.h"

#include <stdlib.h>

/* ================================================================
 *  内部结构体
 * ================================================================ */

/** @brief PAL LDO 内部状态 */
typedef struct {
    esp_ldo_channel_handle_t chan;   /**< 底层 LDO 通道句柄 */
} pal_ldo_internal_t;

/* ================================================================
 *  生命周期 API
 * ================================================================ */

int pal_ldo_acquire(pal_ldo_handle_t *handle, const pal_ldo_config_t *cfg)
{
    if (handle == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pal_ldo_internal_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = cfg->chan_id,
        .voltage_mv = (int)cfg->voltage_mv,
    };
    esp_err_t ret = esp_ldo_acquire_channel(&ldo_cfg, &ctx->chan);
    if (ret != ESP_OK) {
        free(ctx);
        return ret;
    }

    *handle = (pal_ldo_handle_t)ctx;
    return ESP_OK;
}

int pal_ldo_release(pal_ldo_handle_t handle)
{
    pal_ldo_internal_t *ctx = (pal_ldo_internal_t *)handle;
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    if (ctx->chan != NULL) {
        ret = esp_ldo_release_channel(ctx->chan);
        ctx->chan = NULL;
    }
    free(ctx);
    return ret;
}

/* ================================================================
 *  运行时 API
 * ================================================================ */

int pal_ldo_set_voltage(pal_ldo_handle_t handle, uint32_t voltage_mv)
{
    pal_ldo_internal_t *ctx = (pal_ldo_internal_t *)handle;
    if (ctx == NULL || ctx->chan == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_ldo_channel_adjust_voltage(ctx->chan, (int)voltage_mv);
}

void *pal_ldo_get_handle(pal_ldo_handle_t handle)
{
    pal_ldo_internal_t *ctx = (pal_ldo_internal_t *)handle;
    if (ctx == NULL) {
        return NULL;
    }
    return ctx->chan;
}
