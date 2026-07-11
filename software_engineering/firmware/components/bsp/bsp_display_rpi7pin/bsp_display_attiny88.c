/**
 * @file    bsp_display_attiny88.c
 * @brief   ATTINY88 背光/电源管理 MCU 子驱动实现
 *
 * @details 实现 ATTINY88 的电源上电序列、复位释放、背光控制。
 *          使用 ESP-IDF driver/i2c_master.h 原生 API（i2c_master_bus_add_device
 *          / i2c_master_transmit / i2c_master_transmit_receive），延时用
 *          FreeRTOS vTaskDelay。所有 esp_err_t 在本边界翻译为 dal_err_t。
 *
 * @author  xLumina
 * @version 2.0
 */
#include "bsp_display_attiny88.h"
#include "board_v1_config.h"
#include "dal_esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/** I2C 传输超时（ms），-1 = 永久等待 */
#define ATTINY88_I2C_TIMEOUT_MS   100

static const char *TAG = "ATTINY88";

/* ================================================================
 *  内部：寄存器读写（原生 i2c_master）
 * ================================================================ */

/** @brief 单字节寄存器写（reg + val 两字节 I2C 写） */
static dal_err_t attiny88_write_reg(bsp_attiny88_ctx_t *ctx,
                                    uint8_t reg, uint8_t val)
{
    if (ctx == NULL || ctx->dev == NULL) {
        return DAL_ERR_INVALID;
    }
    uint8_t buf[2] = { reg, val };
    return dal_err_from_esp(i2c_master_transmit(ctx->dev, buf, sizeof(buf),
                                                ATTINY88_I2C_TIMEOUT_MS));
}

/** @brief 单字节寄存器读（先发 reg 再收 1 字节） */
static dal_err_t attiny88_read_reg(bsp_attiny88_ctx_t *ctx,
                                   uint8_t reg, uint8_t *val)
{
    if (ctx == NULL || ctx->dev == NULL || val == NULL) {
        return DAL_ERR_INVALID;
    }
    return dal_err_from_esp(i2c_master_transmit_receive(ctx->dev, &reg, 1,
                                                       val, 1,
                                                       ATTINY88_I2C_TIMEOUT_MS));
}

/* ================================================================
 *  生命周期
 * ================================================================ */

dal_err_t bsp_attiny88_init(bsp_attiny88_ctx_t *ctx, i2c_master_bus_handle_t bus)
{
    if (ctx == NULL || bus == NULL) {
        return DAL_ERR_INVALID;
    }

    ctx->i2c_addr = BOARD_DISPLAY_ATTINY88_I2C_ADDR;
    ctx->inited   = false;
    ctx->dev      = NULL;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ctx->i2c_addr,
        .scl_speed_hz    = BOARD_I2C_FREQ_HZ,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &ctx->dev);
    if (ret != ESP_OK) {
        return dal_err_from_esp(ret);
    }

    /* 读固件 ID 校验（失败仅告警，不阻断——某些板可能未烧 v2 固件） */
    uint8_t id = 0;
    if (attiny88_read_reg(ctx, ATTINY88_REG_ID, &id) == DAL_OK) {
        ESP_LOGI(TAG, "firmware id: 0x%02X%s",
                 id, (id == ATTINY88_FW_ID_V2) ? " (v2)" : "");
    } else {
        ESP_LOGW(TAG, "read id failed, continue");
    }

    /* 初始状态：复位全部保持、电源关闭、背光关闭 */
    attiny88_write_reg(ctx, ATTINY88_REG_PORTC, 0x00);
    attiny88_write_reg(ctx, ATTINY88_REG_PORTB, 0x00);
    attiny88_write_reg(ctx, ATTINY88_REG_PWM,   0x00);

    ctx->inited = true;
    return DAL_OK;
}

dal_err_t bsp_attiny88_deinit(bsp_attiny88_ctx_t *ctx)
{
    if (ctx == NULL) {
        return DAL_ERR_INVALID;
    }
    if (ctx->dev != NULL) {
        if (ctx->inited) {
            attiny88_write_reg(ctx, ATTINY88_REG_PWM,   0x00);
            attiny88_write_reg(ctx, ATTINY88_REG_PORTC, 0x00);
            attiny88_write_reg(ctx, ATTINY88_REG_PORTB, 0x00);
        }
        i2c_master_bus_rm_device(ctx->dev);
        ctx->dev = NULL;
    }
    ctx->inited = false;
    return DAL_OK;
}

dal_err_t bsp_attiny88_power_on(bsp_attiny88_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }

    dal_err_t ret;
    /* 1. 扫描方向 + 主电源开 + LED 使能（桥/LCD/触控复位保持低） */
    ret  = attiny88_write_reg(ctx, ATTINY88_REG_PORTA, ATTINY88_PORTA_SCAN_LR);
    ret |= attiny88_write_reg(ctx, ATTINY88_REG_PORTB, ATTINY88_PORTB_POWER_ON);
    ret |= attiny88_write_reg(ctx, ATTINY88_REG_PORTC, ATTINY88_PORTC_LED_EN);
    if (ret != DAL_OK) {
        return ret;
    }

    /* 2. 等待电源稳定 */
    vTaskDelay(pdMS_TO_TICKS(BOARD_DISPLAY_POWER_TO_BACKLIGHT_MS));

    ESP_LOGI(TAG, "main power on (bridge reset held)");
    return DAL_OK;
}

dal_err_t bsp_attiny88_release_reset(bsp_attiny88_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }

    /* PORTC：LED 使能 + 释放桥/LCD/触控复位（全部置 1） */
    uint8_t val = ATTINY88_PORTC_LED_EN
                | ATTINY88_PORTC_LCD_RST
                | ATTINY88_PORTC_BRIDGE_RST
                | ATTINY88_PORTC_TOUCH_RST;
    dal_err_t ret = attiny88_write_reg(ctx, ATTINY88_REG_PORTC, val);
    if (ret != DAL_OK) {
        return ret;
    }

    /* 复位释放后等待桥就绪 */
    vTaskDelay(pdMS_TO_TICKS(BOARD_DISPLAY_POWER_TO_BACKLIGHT_MS));
    ESP_LOGI(TAG, "bridge/lcd/touch reset released");
    return DAL_OK;
}

dal_err_t bsp_attiny88_set_backlight(bsp_attiny88_ctx_t *ctx, uint8_t percent)
{
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }
    /* 0~100% 映射到 0~255 */
    uint8_t val = (uint8_t)((uint16_t)percent * 255u / 100u);
    return attiny88_write_reg(ctx, ATTINY88_REG_PWM, val);
}
