/**
 * @file    board_v1.c
 * @brief   Board V1 板级组装器 — 共享 I2C 总线 + 各 BSP create/register
 *
 * @details 板级组装器职责（架构规范 §3.5）：
 *          1. 初始化共享 I2C 总线（display/camera/touch/audio 共用 SDA=7/SCL=8）
 *          2. 按依赖顺序调用各 BSP 的 bsp_xxx_create() 获取 ops（ctx 已注入 ops->ctx）
 *          3. 调用 dal_xxx_register(name, ops, ops->ctx) 完成设备注册
 *
 *          本组装器**不触发硬件初始化**——create() 零副作用，硬件 init 由
 *          上层（main）通过 ops->init(ops->ctx) 按需触发。直接使用 ESP-IDF
 *          driver/i2c_master 原生 API 管理共享总线，不经 PAL。
 *
 * @author  xiamu
 * @version 1.2
 */
#include "board_v1.h"
#include "board_v1_config.h"

#include "bsp_storage_sdmmc.h"
#include "bsp_relay.h"
#include "bsp_display_rpi7pin.h"
#include "bsp_camera_ov5647.h"
#include "bsp_touch_ft5406.h"
#include "bsp_audio_es8311.h"
#include "bsp_network_ip101.h"
#include "bsp_pir_hcsr501.h"

#include "dal_display.h"
#include "dal_camera.h"
#include "dal_touch.h"
#include "dal_audio.h"
#include "dal_network.h"
#include "dal_pir.h"
#include "dal_relay.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "bsp_v1";

/* 共享 I2C 总线句柄（静态全局，board_i2c_get_bus 返回） */
static i2c_master_bus_handle_t s_i2c_bus = NULL;

void *board_i2c_get_bus(void)
{
    return s_i2c_bus;
}

/** 初始化共享 I2C 总线（ESP-IDF 原生 i2c_master） */
static void board_i2c_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port           = BOARD_I2C_PORT,
        .sda_io_num         = BOARD_I2C_SDA_PIN,
        .scl_io_num         = BOARD_I2C_SCL_PIN,
        .clk_source         = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt  = 7,
        .intr_priority      = BOARD_I2C_INTR_PRIORITY,
        .trans_queue_depth  = BOARD_I2C_TRANS_QUEUE_DEPTH,
        .flags = {
            .enable_internal_pullup = BOARD_I2C_ENABLE_PULLUP,
        },
    };
    esp_err_t e = i2c_new_master_bus(&cfg, &s_i2c_bus);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "shared I2C bus init failed: %d", e);
        return;
    }
    ESP_LOGI(TAG, "shared I2C bus ready (SDA=%d SCL=%d)",
             BOARD_I2C_SDA_PIN, BOARD_I2C_SCL_PIN);
}

dal_err_t board_v1_init(void)
{
    /* 1. 共享 I2C 总线（display/camera/touch/audio 依赖） */
    board_i2c_init();

    /* 2. SDMMC 存储挂载（板级基础设施，非 DAL 设备，立即挂载） */
    if (bsp_storage_sdmmc_init() != DAL_OK) {
        ESP_LOGW(TAG, "storage mount failed");
    }

    /* 3. 显示屏 — create + register（依赖 I2C 初始化 ATTINY88） */
    {
        dal_display_ops_t *ops = bsp_display_rpi7pin_create();
        if (ops && dal_display_register("rpi7pin", ops, ops->ctx) != DAL_OK) {
            ESP_LOGW(TAG, "display register failed");
        }
    }

    /* 4. 摄像头 — create + register（依赖 I2C 的 SCCB） */
    {
        dal_camera_ops_t *ops = bsp_camera_ov5647_create();
        if (ops && dal_camera_register("main_cam", ops, ops->ctx) != DAL_OK) {
            ESP_LOGW(TAG, "camera register failed");
        }
    }

    /* 5. 触摸 — create + register（依赖 I2C） */
    {
        dal_touch_ops_t *ops = bsp_touch_ft5406_create();
        if (ops && dal_touch_register("main_touch", ops, ops->ctx) != DAL_OK) {
            ESP_LOGW(TAG, "touch register failed");
        }
    }

    /* 6. 继电器 — 3 实例，各 create + register */
    {
        dal_relay_ops_t *ops;
        ops = bsp_relay_create("door_lock");
        if (ops) dal_relay_register("door_lock", ops, ops->ctx);
        ops = bsp_relay_create("alarm");
        if (ops) dal_relay_register("alarm", ops, ops->ctx);
        ops = bsp_relay_create("wiegand_pwr");
        if (ops) dal_relay_register("wiegand_pwr", ops, ops->ctx);
    }

    /* 7. 音频 — create + register（依赖 I2C 初始化 ES8311 codec） */
    {
        dal_audio_ops_t *ops = bsp_audio_es8311_create();
        if (ops && dal_audio_register("main_audio", ops, ops->ctx) != DAL_OK) {
            ESP_LOGW(TAG, "audio register failed");
        }
    }

    /* 8. 以太网 — create + register（独立，无 I2C 依赖） */
    {
        dal_network_ops_t *ops = bsp_network_ip101_create();
        if (ops && dal_network_register("main_eth", ops, ops->ctx) != DAL_OK) {
            ESP_LOGW(TAG, "network register failed");
        }
    }

    /* 9. PIR 人体感应 — create + register（GPIO 双沿中断由 service 调 set_edge_cb 启用） */
    {
        dal_pir_ops_t *ops = bsp_pir_hcsr501_create();
        if (ops && dal_pir_register("main_pir", ops, ops->ctx) != DAL_OK) {
            ESP_LOGW(TAG, "pir register failed");
        }
    }

    ESP_LOGI(TAG, "board assemble done (create + register)");
    return DAL_OK;
}
