/**
 * @file    bsp_storage_sdmmc.c
 * @brief   V1 板卡 SDMMC 存储初始化实现（ESP-IDF 原生，去 PAL）
 *
 * @details 直接调用 ESP-IDF esp_vfs_fat_sdmmc 挂载 SD 卡 FAT 文件系统到
 *          /sdcard。不封装 DAL，业务层用标准 stdio（fopen/fread/...）。
 *          esp_err_t 在 BSP 边界经 dal_err_from_esp() 翻译为 dal_err_t。
 *
 *          挂载序列等价于原 pal_sdmmc.c：片上 LDO 供电(LDO_VO4/3.3V) →
 *          slot 引脚/宽度/内部上拉配置 → esp_vfs_fat_sdmmc_mount（带降频
 *          重试）。引脚、频率、挂载点、宽度均取自 board_v1_config.h 的
 *          BOARD_STORAGE_* 宏。
 *
 * @author  xLumina
 * @version 1.1
 */
#include "bsp_storage_sdmmc.h"
#include "dal_esp_err.h"
#include "board_v1_config.h"

#include "driver/sdmmc_host.h"
#include "driver/sdmmc_default_configs.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_vfs_fat.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"

#include <stdbool.h>

static const char *SD_TAG = "BSP_SDMMC";

/** SD 卡上电稳定时间：LDO 供电后需时间稳定 */
#define SD_POWER_STABLE_MS  200
/** 降频重试次数（首次配置频率，后续 400kHz probing） */
#define SD_RETRY_COUNT      2
/** ESP32-P4 片上 LDO 通道：LDO_VO4 连接 SDMMC IO 供电（3.3V） */
#define SD_PWR_LDO_CHAN_ID  4
/** FAT 簇大小（与原 PAL 一致） */
#define SD_ALLOCATION_UNIT  (16 * 1024)

/* 静态状态：挂载后生命周期贯穿系统运行期（无 unmount 接口）。
 * 保留 card/pwr_ctrl 句柄供后续可能的卸载/查询使用。 */
static bool                  s_mounted  = false;
static sdmmc_card_t         *s_card     = NULL;
static sd_pwr_ctrl_handle_t  s_pwr_ctrl = NULL;

dal_err_t bsp_storage_sdmmc_init(void)
{
    /* 幂等：已挂载直接返回成功 */
    if (s_mounted) {
        return DAL_OK;
    }

    /* ---- 0. 片上 LDO 供电：ESP32-P4 SDMMC IO 由 LDO_VO4 供电(3.3V) ----
     * ⚠️ 根因修复：此前未配置 SD 电源，SD 卡无供电 → CMD 无响应 →
     *    send_op_cond 超时(0x107)。必须先用 sd_pwr_ctrl_new_on_chip_ldo
     *    使能 LDO 通道 4，并把句柄挂到 host.pwr_ctrl_handle。 */
    sd_pwr_ctrl_ldo_config_t ldo_cfg = {
        .ldo_chan_id = SD_PWR_LDO_CHAN_ID,
    };
    esp_err_t ldo_ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &s_pwr_ctrl);
    if (ldo_ret == ESP_OK) {
        ESP_LOGI(SD_TAG, "SD LDO power enabled (chan=%d, 3.3V)", SD_PWR_LDO_CHAN_ID);
    } else {
        ESP_LOGW(SD_TAG, "LDO init failed: %s (continue)", esp_err_to_name(ldo_ret));
        s_pwr_ctrl = NULL;
    }
    /* LDO 上电后等待供电稳定 */
    vTaskDelay(pdMS_TO_TICKS(SD_POWER_STABLE_MS));

    /* ---- 1. slot 配置（P4 引脚 + 宽度 + 内部上拉） ----
     * 启用内部上拉：无外部上拉时 CMD/DAT 线浮空会导致卡无响应。
     * 宽度由 BOARD_STORAGE_USE_1BIT 决定（false=4-bit）。 */
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = (gpio_num_t)BOARD_STORAGE_CLK_PIN;
    slot.cmd = (gpio_num_t)BOARD_STORAGE_CMD_PIN;
    slot.d0  = (gpio_num_t)BOARD_STORAGE_D0_PIN;
    slot.d1  = (gpio_num_t)BOARD_STORAGE_D1_PIN;
    slot.d2  = (gpio_num_t)BOARD_STORAGE_D2_PIN;
    slot.d3  = (gpio_num_t)BOARD_STORAGE_D3_PIN;
    slot.width = BOARD_STORAGE_USE_1BIT ? 1 : 4;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    /* ---- 2. FAT 挂载配置 ---- */
    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = BOARD_STORAGE_FORMAT_IF_MOUNT_FAILED,
        .max_files              = BOARD_STORAGE_MAX_FILES,
        .allocation_unit_size   = SD_ALLOCATION_UNIT,
    };

    /* ---- 3. 挂载（带降频重试） ----
     * 首次用配置频率；失败则降为 400kHz probing 重试，提升边缘卡/弱信号
     * 场景的鲁棒性。每次重试前重新等待供电稳定。 */
    esp_err_t ret = ESP_FAIL;
    for (int attempt = 0; attempt < SD_RETRY_COUNT; attempt++) {
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.pwr_ctrl_handle = s_pwr_ctrl;   /* 挂载 LDO 供电句柄 */
        if (attempt == 0) {
            /* BOARD_STORAGE_FREQ_KHZ 单位为 kHz，与 max_freq_khz 字段一致，直接赋值。
             * （原 pal_sdmmc.c 误把该值当 Hz 再 /1000，实际跑到 20kHz；去 PAL 后按
             *   宏名义值 20MHz 使用，与 SDMMC_FREQ_DEFAULT 一致。） */
            host.max_freq_khz = BOARD_STORAGE_FREQ_KHZ;
        } else {
            /* 降频重试：400kHz probing，最保守 */
            host.max_freq_khz = SDMMC_FREQ_PROBING;
            ESP_LOGW(SD_TAG, "retry mount at 400kHz probing (attempt %d)",
                     attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(SD_POWER_STABLE_MS));
        }

        ret = esp_vfs_fat_sdmmc_mount(BOARD_STORAGE_MOUNT_POINT, &host, &slot,
                                      &mount_cfg, &s_card);
        if (ret == ESP_OK) {
            break;
        }
        ESP_LOGW(SD_TAG, "mount attempt %d failed: %s",
                 attempt + 1, esp_err_to_name(ret));
    }

    if (ret != ESP_OK) {
        if (s_pwr_ctrl) {
            sd_pwr_ctrl_del_on_chip_ldo(s_pwr_ctrl);
            s_pwr_ctrl = NULL;
        }
        s_card = NULL;
        return dal_err_from_esp(ret);
    }

    s_mounted = true;
    ESP_LOGI(SD_TAG, "mounted: %s", BOARD_STORAGE_MOUNT_POINT);
    return DAL_OK;
}
