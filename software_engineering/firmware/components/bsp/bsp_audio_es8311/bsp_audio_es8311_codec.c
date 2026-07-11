/**
 * @file    bsp_audio_es8311_codec.c
 * @brief   ES8311 音频 CODEC 子驱动实现
 *
 * @details 移植参考工程的 ES8311 寄存器初始化序列，适配当前工程的
 *          BSP 私有 + dal_err_t + 共享总线模型。直接调用 ESP-IDF
 *          driver/i2c_master，esp_err_t 经 dal_err_from_esp 翻译为
 *          dal_err_t，不透传到上层。
 *
 * @author  xLumina
 * @version 1.1
 */
#include "bsp_audio_es8311_codec.h"
#include "dal_esp_err.h"
#include "board_v1_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---- 内部：寄存器写（reg + val 两字节 I2C transmit）---- */
static dal_err_t es8311_write_reg(bsp_es8311_ctx_t *ctx, uint8_t reg, uint8_t val)
{
    if (ctx == NULL || ctx->dev == NULL) {
        return DAL_ERR_INVALID;
    }
    uint8_t buf[2] = { reg, val };
    return dal_err_from_esp(i2c_master_transmit(ctx->dev, buf, sizeof(buf), -1));
}

dal_err_t bsp_es8311_init(bsp_es8311_ctx_t *ctx, i2c_master_bus_handle_t bus)
{
    if (ctx == NULL || bus == NULL) {
        return DAL_ERR_INVALID;
    }

    /* 在共享总线上挂载 ES8311 设备 */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BOARD_AUDIO_CODEC_I2C_ADDR,
        .scl_speed_hz    = BOARD_I2C_FREQ_HZ,
        .scl_wait_us     = 0,   /* 使用驱动默认值 */
        .flags = {
            .disable_ack_check = false,
        },
    };
    esp_err_t e = i2c_master_bus_add_device(bus, &dev_cfg, &ctx->dev);
    if (e != ESP_OK) {
        return dal_err_from_esp(e);
    }

    /* 1. 复位并进入 I2S slave 模式 */
    dal_err_t r;
    r  = es8311_write_reg(ctx, ES8311_REG_SYS0D,    0xFA);
    r |= es8311_write_reg(ctx, ES8311_REG_GPIO44,   0x08);
    r |= es8311_write_reg(ctx, ES8311_REG_GPIO44,   0x08);
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR1, 0x30);
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR2, 0x00);
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR3, 0x10);
    r |= es8311_write_reg(ctx, ES8311_REG_ADC16,    0x24);
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR4, 0x10);
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR5, 0x00);
    r |= es8311_write_reg(ctx, ES8311_REG_SYS0B,    0x00);
    r |= es8311_write_reg(ctx, ES8311_REG_SYS0C,    0x00);
    r |= es8311_write_reg(ctx, ES8311_REG_SYS10,    0x1F);
    r |= es8311_write_reg(ctx, ES8311_REG_SYS11,    0x7F);
    r |= es8311_write_reg(ctx, ES8311_REG_RESET,    0x80);   /* slave mode */
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR1, 0x3F);   /* 外部 MCLK */
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR6, 0x00);
    r |= es8311_write_reg(ctx, ES8311_REG_SYS13,    0x10);
    r |= es8311_write_reg(ctx, ES8311_REG_ADC1B,    0x0A);
    r |= es8311_write_reg(ctx, ES8311_REG_ADC1C,    0x6A);
    r |= es8311_write_reg(ctx, ES8311_REG_GPIO44,   0x58);   /* 内部参考 */

    /* 2. 16 kHz / MCLK=256fs / 16-bit I2S Philips */
    r |= es8311_write_reg(ctx, ES8311_REG_SDP_IN,   0x0C);
    r |= es8311_write_reg(ctx, ES8311_REG_SDP_OUT,  0x0C);
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR2, 0x00);
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR5, 0x00);
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR3, 0x10);
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR4, 0x20);
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR7, 0x00);
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR8, 0xFF);
    r |= es8311_write_reg(ctx, ES8311_REG_CLK_MGR6, 0x03);

    /* 3. 启用 DAC 输出并取消静音 */
    r |= es8311_write_reg(ctx, ES8311_REG_ADC17,    0xBF);
    r |= es8311_write_reg(ctx, ES8311_REG_SYS0E,    0x02);
    r |= es8311_write_reg(ctx, ES8311_REG_SYS12,    0x00);
    r |= es8311_write_reg(ctx, ES8311_REG_SYS14,    0x1A);
    r |= es8311_write_reg(ctx, ES8311_REG_SYS0D,    0x01);
    r |= es8311_write_reg(ctx, ES8311_REG_ADC15,    0x40);
    r |= es8311_write_reg(ctx, ES8311_REG_DAC37,    0x08);
    r |= es8311_write_reg(ctx, ES8311_REG_GP45,     0x00);
    r |= es8311_write_reg(ctx, ES8311_REG_DAC31,    0x00);
    r |= es8311_write_reg(ctx, ES8311_REG_VOLUME,   ES8311_VOLUME_MAX);

    if (r != DAL_OK) {
        ESP_LOGE("ES8311", "init reg sequence failed");
        i2c_master_bus_rm_device(ctx->dev);
        ctx->dev = NULL;
        return r;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    ctx->inited = true;
    ESP_LOGI("ES8311", "codec initialized");
    return DAL_OK;
}

dal_err_t bsp_es8311_deinit(bsp_es8311_ctx_t *ctx)
{
    if (ctx == NULL) {
        return DAL_ERR_INVALID;
    }
    if (ctx->dev != NULL) {
        if (ctx->inited) {
            es8311_write_reg(ctx, ES8311_REG_RESET, 0x00);
        }
        i2c_master_bus_rm_device(ctx->dev);
        ctx->dev = NULL;
    }
    ctx->inited = false;
    return DAL_OK;
}

dal_err_t bsp_es8311_set_volume(bsp_es8311_ctx_t *ctx, uint8_t percent)
{
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }
    /* 0~100% 映射到 0x00~0xC0 */
    uint8_t val = (uint8_t)((uint16_t)percent * ES8311_VOLUME_MAX / 100u);
    return es8311_write_reg(ctx, ES8311_REG_VOLUME, val);
}

dal_err_t bsp_es8311_set_mute(bsp_es8311_ctx_t *ctx, bool mute)
{
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }
    return es8311_write_reg(ctx, ES8311_REG_DAC31, mute ? 0x60 : 0x00);
}
