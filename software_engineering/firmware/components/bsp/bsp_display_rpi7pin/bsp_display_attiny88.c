/**
 * @file    bsp_display_attiny88.c
 * @brief   ATTINY88 背光/电源管理 MCU 子驱动实现
 *
 * @details 实现 ATTINY88 的电源上电序列、复位释放、背光控制。
 *          所有 PAL I2C 返回码经 dal_err_from_pal 翻译为 dal_err_t。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "bsp_display_attiny88.h"
#include "dal_pal_err.h"
#include "bsp_config.h"
#include "pal_log.h"
#include "osal_task.h"

/* ---- 内部：单字节寄存器写（reg + val 两字节 I2C 写）---- */
static dal_err_t attiny88_write_reg(bsp_attiny88_ctx_t *ctx,
                                    uint8_t reg, uint8_t val)
{
    if (ctx == NULL || ctx->dev == NULL) {
        return DAL_ERR_INVALID;
    }
    uint8_t buf[2] = { reg, val };
    return dal_err_from_pal(pal_i2c_write(ctx->dev, buf, sizeof(buf)));
}

/* ---- 内部：单字节寄存器读 ---- */
static dal_err_t attiny88_read_reg(bsp_attiny88_ctx_t *ctx,
                                   uint8_t reg, uint8_t *val)
{
    if (ctx == NULL || ctx->dev == NULL || val == NULL) {
        return DAL_ERR_INVALID;
    }
    return dal_err_from_pal(pal_i2c_read_reg(ctx->dev, reg, val, 1));
}

dal_err_t bsp_attiny88_init(bsp_attiny88_ctx_t *ctx, pal_i2c_bus_handle_t bus)
{
    if (ctx == NULL || bus == NULL) {
        return DAL_ERR_INVALID;
    }

    ctx->i2c_addr = BOARD_DISPLAY_ATTINY88_I2C_ADDR;
    ctx->inited   = false;

    pal_i2c_dev_config_t dev_cfg = {
        .device_address    = ctx->i2c_addr,
        .scl_speed_hz      = BOARD_I2C_FREQ_HZ,
        .disable_ack_check = false,
    };
    int ret = pal_i2c_dev_attach(&ctx->dev, bus, &dev_cfg);
    if (ret != 0) {
        return dal_err_from_pal(ret);
    }

    /* 读固件 ID 校验（失败仅告警，不阻断——某些板可能未烧 v2 固件） */
    uint8_t id = 0;
    if (attiny88_read_reg(ctx, ATTINY88_REG_ID, &id) == DAL_OK) {
        PAL_LOGI("ATTINY88", "firmware id: 0x%02X%s",
                 id, (id == ATTINY88_FW_ID_V2) ? " (v2)" : "");
    } else {
        PAL_LOGW("ATTINY88", "read id failed, continue");
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
        pal_i2c_dev_detach(ctx->dev);
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
    osal_task_delay_ms(BOARD_DISPLAY_POWER_TO_BACKLIGHT_MS);

    PAL_LOGI("ATTINY88", "main power on (bridge reset held)");
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
    osal_task_delay_ms(BOARD_DISPLAY_POWER_TO_BACKLIGHT_MS);
    PAL_LOGI("ATTINY88", "bridge/lcd/touch reset released");
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
