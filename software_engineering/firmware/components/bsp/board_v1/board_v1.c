/**
 * @file    board_v1.c
 * @brief   Board V1 板级初始化 — 共享 I2C 总线管理 + 各 BSP 按序初始化
 *
 * @details 统一初始化共享 I2C 总线（display/camera/touch 共用 SDA=7/SCL=8），
 *          避免各 BSP 重复初始化。然后按依赖顺序调用各 BSP init（触发其
 *          自注册到 DAL）。任一非关键失败记录日志但不中止后续初始化，
 *          返回 false 表示整体存在异常。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "board_v1.h"
#include "bsp_config.h"
#include "bsp_storage_sdmmc.h"
#include "bsp_relay.h"
#include "bsp_display_rpi7pin.h"
#include "bsp_camera_ov5647.h"
#include "bsp_touch_ft5406.h"
#include "bsp_audio_es8311.h"
#include "bsp_network_ip101.h"
#include "bsp_pir_hcsr501.h"
#include "pal_i2c.h"
#include "pal_log.h"

/* 共享 I2C 总线句柄（静态全局，board_i2c_get_bus 返回） */
static pal_i2c_bus_handle_t s_i2c_bus = NULL;

void *board_i2c_get_bus(void)
{
    return s_i2c_bus;
}

/** 初始化共享 I2C 总线 */
static bool board_i2c_init(void)
{
    pal_i2c_bus_config_t cfg = {
        .port                  = BOARD_I2C_PORT,
        .sda_pin               = BOARD_I2C_SDA_PIN,
        .scl_pin               = BOARD_I2C_SCL_PIN,
        .freq_hz               = BOARD_I2C_FREQ_HZ,
        .enable_internal_pullup= BOARD_I2C_ENABLE_PULLUP,
        .intr_priority         = BOARD_I2C_INTR_PRIORITY,
        .trans_queue_depth     = BOARD_I2C_TRANS_QUEUE_DEPTH,
    };
    int ret = pal_i2c_bus_init(&s_i2c_bus, &cfg);
    if (ret != 0) {
        PAL_LOGE("BOARD", "shared I2C bus init failed: %d", ret);
        return false;
    }
    PAL_LOGI("BOARD", "shared I2C bus ready (SDA=%d SCL=%d)",
             BOARD_I2C_SDA_PIN, BOARD_I2C_SCL_PIN);
    return true;
}

bool board_v1_init(void)
{
    bool ok = true;

    /* 1. 共享 I2C 总线（display/camera/touch 依赖） */
    if (!board_i2c_init()) {
        ok = false;
    }

    /* 2. SDMMC 存储 */
    if (bsp_storage_sdmmc_init() != DAL_OK) {
        PAL_LOGW("BOARD", "storage init failed");
        ok = false;
    }

    /* 3. 显示屏（依赖 I2C 初始化 ATTINY88） */
    if (bsp_display_rpi7pin_init() != DAL_OK) {
        PAL_LOGW("BOARD", "display init failed");
        ok = false;
    }

    /* 4. 摄像头（依赖 I2C 的 SCCB） */
    if (bsp_camera_ov5647_init() != DAL_OK) {
        PAL_LOGW("BOARD", "camera init failed");
        ok = false;
    }

    /* 5. 触摸（依赖 I2C） */
    if (bsp_touch_ft5406_init() != DAL_OK) {
        PAL_LOGW("BOARD", "touch init failed");
        ok = false;
    }

    /* 6. 继电器 */
    if (bsp_relay_init() != DAL_OK) {
        PAL_LOGW("BOARD", "relay init failed");
        ok = false;
    }

    /* 7. 音频（依赖 I2C 初始化 ES8311 codec） */
    if (bsp_audio_es8311_init() != DAL_OK) {
        PAL_LOGW("BOARD", "audio init failed");
        ok = false;
    }

    /* 8. 以太网（独立，无 I2C 依赖） */
    if (bsp_network_ip101_init() != DAL_OK) {
        PAL_LOGW("BOARD", "network init failed");
        ok = false;
    }

    /* 9. PIR 人体感应（依赖 GPIO ISR 服务，内部自安装） */
    if (bsp_pir_hcsr501_init() != DAL_OK) {
        PAL_LOGW("BOARD", "pir init failed");
        ok = false;
    }

    PAL_LOGI("BOARD", "board init %s", ok ? "ok" : "completed with warnings");
    return ok;
}
