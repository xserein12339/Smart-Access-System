/**
 * @file    bsp_storage_sdmmc.c
 * @brief   V1 板卡 SDMMC 存储初始化实现
 *
 * @details 挂载 SDMMC FAT 文件系统到 /sdcard。不封装 DAL，业务层用 stdio。
 *          PAL 返回码经 dal_err_from_pal 翻译为 dal_err_t。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "bsp_storage_sdmmc.h"
#include "dal_pal_err.h"
#include "pal_sdmmc.h"
#include "bsp_config.h"

/* 静态句柄，生命周期贯穿系统运行期 */
static pal_sdmmc_handle_t s_sd_handle = NULL;

dal_err_t bsp_storage_sdmmc_init(void)
{
    /* 幂等：已挂载直接返回成功 */
    if (s_sd_handle != NULL) {
        return DAL_OK;
    }

    const pal_sdmmc_config_t cfg = {
        .clk_pin = BOARD_STORAGE_CLK_PIN,
        .cmd_pin = BOARD_STORAGE_CMD_PIN,
        .d0_pin  = BOARD_STORAGE_D0_PIN,
        .d1_pin  = BOARD_STORAGE_D1_PIN,
        .d2_pin  = BOARD_STORAGE_D2_PIN,
        .d3_pin  = BOARD_STORAGE_D3_PIN,
        .bus_width = 4,
        .freq_hz   = BOARD_STORAGE_FREQ_KHZ,
        .mount_point = BOARD_STORAGE_MOUNT_POINT,
        .format_if_mount_failed = BOARD_STORAGE_FORMAT_IF_MOUNT_FAILED,
        .max_files = BOARD_STORAGE_MAX_FILES,
    };

    int ret = pal_sdmmc_mount(&s_sd_handle, &cfg);
    if (ret != 0) {
        return dal_err_from_pal(ret);
    }
    return DAL_OK;
}
