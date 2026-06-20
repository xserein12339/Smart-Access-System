/**
 * @file    pal_sdmmc.c
 * @brief   PAL SDMMC 模块 - 实现（ESP-IDF esp_vfs_fat_sdmmc 封装）
 *
 * 使用 SDMMC_HOST_DEFAULT + SDMMC_SLOT_CONFIG_DEFAULT 作为基线配置，
 * 按用户配置覆盖引脚 / 宽度 / 频率，最终调用 esp_vfs_fat_sdmmc_mount 完成挂载。
 */

#include "pal_sdmmc.h"

#include "driver/sdmmc_host.h"
#include "driver/sdmmc_default_configs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_err.h"
#include "hal/gpio_types.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  内部结构体
 * ================================================================ */

/** @brief PAL SDMMC 内部状态 */
typedef struct {
    sdmmc_card_t *card;     /**< SD 卡信息结构体 */
    char         *mount_point; /**< 挂载点副本（unmount 时需用） */
} pal_sdmmc_internal_t;

/* ================================================================
 *  生命周期 API
 * ================================================================ */

int pal_sdmmc_mount(pal_sdmmc_handle_t *handle, const pal_sdmmc_config_t *cfg)
{
    if (handle == NULL || cfg == NULL || cfg->mount_point == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pal_sdmmc_internal_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ctx->mount_point = strdup(cfg->mount_point);
    if (ctx->mount_point == NULL) {
        free(ctx);
        return ESP_ERR_NO_MEM;
    }

    /* ---- 1. 主机配置（默认 + 可选频率覆盖） ---- */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    if (cfg->freq_hz != 0) {
        host.max_freq_khz = cfg->freq_hz / 1000;
    }

    /* ---- 2. slot 配置（默认 P4 引脚 + 用户覆盖） ---- */
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    if (cfg->clk_pin >= 0) slot.clk = (gpio_num_t)cfg->clk_pin;
    if (cfg->cmd_pin >= 0) slot.cmd = (gpio_num_t)cfg->cmd_pin;
    if (cfg->d0_pin  >= 0) slot.d0  = (gpio_num_t)cfg->d0_pin;
    if (cfg->d1_pin  >= 0) slot.d1  = (gpio_num_t)cfg->d1_pin;
    if (cfg->d2_pin  >= 0) slot.d2  = (gpio_num_t)cfg->d2_pin;
    if (cfg->d3_pin  >= 0) slot.d3  = (gpio_num_t)cfg->d3_pin;
    if (cfg->bus_width > 0) {
        slot.width = (uint8_t)cfg->bus_width;
    }

    /* ---- 3. FAT 挂载配置 ---- */
    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = cfg->format_if_mount_failed,
        .max_files              = (cfg->max_files > 0) ? cfg->max_files : 5,
        .allocation_unit_size   = 0,
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(cfg->mount_point, &host, &slot,
                                            &mount_cfg, &ctx->card);
    if (ret != ESP_OK) {
        free(ctx->mount_point);
        free(ctx);
        return ret;
    }

    *handle = (pal_sdmmc_handle_t)ctx;
    return ESP_OK;
}

int pal_sdmmc_unmount(pal_sdmmc_handle_t handle)
{
    pal_sdmmc_internal_t *ctx = (pal_sdmmc_internal_t *)handle;
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    if (ctx->card != NULL) {
        ret = esp_vfs_fat_sdcard_unmount(ctx->mount_point, ctx->card);
        ctx->card = NULL;
    }
    free(ctx->mount_point);
    free(ctx);
    return ret;
}

/* ================================================================
 *  信息 API
 * ================================================================ */

void *pal_sdmmc_get_card(pal_sdmmc_handle_t handle)
{
    pal_sdmmc_internal_t *ctx = (pal_sdmmc_internal_t *)handle;
    if (ctx == NULL) {
        return NULL;
    }
    return ctx->card;
}
