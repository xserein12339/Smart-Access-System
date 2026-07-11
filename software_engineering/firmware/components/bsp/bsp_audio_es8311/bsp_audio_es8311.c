/**
 * @file    bsp_audio_es8311.c
 * @brief   ES8311 音频 BSP 实现 — codec + I2S 原生组装，ops + ctx 绑定
 *
 * @details 组装 ES8311 codec（I2C 寄存器配置）与 ESP-IDF driver/i2s_std
 *          （PCM 收发），实现 dal_audio_ops_t 契约。bsp_audio_es8311_create()
 *          仅返回静态 ops 指针（ctx 编译期注入 ops->ctx），不注册 DAL、不驱动
 *          硬件；硬件初始化封装在 ops->init()，由上层按需触发。esp_err_t 在
 *          ops 边界翻译为 dal_err_t，不透传。
 *
 *          - init：从 board 获取共享 I2C 总线初始化 codec；创建 I2S STD
 *            全双工通道（TX/RX，16-bit 立体声主机，参数来自 board_v1_config.h）；
 *            使能 PA。
 *          - play：i2s_channel_write 写 PCM 数据。
 *          - record：i2s_channel_read 读 PCM 数据。
 *          - set_volume/set_mute：转 ES8311 寄存器。
 *
 * @author  xLumina
 * @version 1.2
 */
#include "bsp_audio_es8311.h"
#include "dal_audio_interface.h"
#include "dal_esp_err.h"
#include "bsp_audio_es8311_codec.h"
#include "board_v1_config.h"
#include "board_v1.h"
#include "driver/i2s_std.h"
#include "driver/i2s_types.h"
#include "driver/gpio.h"
#include "hal/gpio_types.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ---- BSP 私有聚合上下文 ---- */
typedef struct {
    bsp_es8311_ctx_t  codec;        /**< ES8311 codec 上下文 */
    i2s_chan_handle_t tx;           /**< I2S 发送通道（无 TX 时为 NULL） */
    i2s_chan_handle_t rx;           /**< I2S 接收通道（无 RX 时为 NULL） */
    gpio_num_t        pa_pin;       /**< PA 使能引脚，GPIO_NUM_NC 无 */
    uint32_t          sample_rate;  /**< 当前采样率 */
    bool              inited;
} bsp_audio_ctx_t;

static bsp_audio_ctx_t s_ctx;

/* ---- I2S 读写超时 ---- */
#define AUDIO_I2S_TIMEOUT_MS  1000u

/* ================================================================
 *  dal_audio_ops_t 实现
 * ================================================================ */
static dal_err_t audio_init(void *ctx_, const dal_audio_config_t *cfg)
{
    bsp_audio_ctx_t *ctx = (bsp_audio_ctx_t *)ctx_;
    if (ctx == NULL || cfg == NULL) {
        return DAL_ERR_INVALID;
    }

    /* 1. 从 board 获取共享 I2C 总线初始化 codec */
    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)board_i2c_get_bus();
    if (bus == NULL) {
        ESP_LOGE("AUDIO", "shared I2C bus not ready");
        return DAL_ERR_STATE;
    }
    dal_err_t ret = bsp_es8311_init(&ctx->codec, bus);
    if (ret != DAL_OK) {
        return ret;
    }

    /* 2. 初始化 I2S STD 全双工主机通道（16-bit 立体声） */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BOARD_AUDIO_I2S_PORT,
                                                            I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 6;
    chan_cfg.dma_frame_num = 240;
    /* intr_priority = 0 使用驱动默认 */

    esp_err_t e = i2s_new_channel(&chan_cfg, &ctx->tx, &ctx->rx);
    if (e != ESP_OK) {
        bsp_es8311_deinit(&ctx->codec);
        return dal_err_from_esp(e);
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(cfg->sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (BOARD_AUDIO_MCLK_PIN < 0) ? GPIO_NUM_NC : (gpio_num_t)BOARD_AUDIO_MCLK_PIN,
            .bclk = (BOARD_AUDIO_BCLK_PIN < 0) ? GPIO_NUM_NC : (gpio_num_t)BOARD_AUDIO_BCLK_PIN,
            .ws   = (BOARD_AUDIO_LRCK_PIN < 0) ? GPIO_NUM_NC : (gpio_num_t)BOARD_AUDIO_LRCK_PIN,
            .dout = (BOARD_AUDIO_DOUT_PIN < 0) ? GPIO_NUM_NC : (gpio_num_t)BOARD_AUDIO_DOUT_PIN,
            .din  = (BOARD_AUDIO_DIN_PIN  < 0) ? GPIO_NUM_NC : (gpio_num_t)BOARD_AUDIO_DIN_PIN,
            .invert_flags = { 0 },
        },
    };
    e = i2s_channel_init_std_mode(ctx->tx, &std_cfg);
    if (e != ESP_OK) {
        goto err_i2s;
    }
    e = i2s_channel_init_std_mode(ctx->rx, &std_cfg);
    if (e != ESP_OK) {
        goto err_i2s;
    }
    i2s_channel_enable(ctx->tx);
    i2s_channel_enable(ctx->rx);

    /* 3. 使能 PA */
    ctx->pa_pin = (gpio_num_t)BOARD_AUDIO_PA_EN_PIN;
    if ((int)ctx->pa_pin >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = BIT64(ctx->pa_pin),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        gpio_set_level(ctx->pa_pin, 1);
    }

    ctx->sample_rate = cfg->sample_rate_hz;
    ctx->inited      = true;

    /* 4. 应用初始音量 */
    bsp_es8311_set_volume(&ctx->codec, cfg->volume);

    ESP_LOGI("AUDIO", "initialized (%lu Hz)", (unsigned long)cfg->sample_rate_hz);
    return DAL_OK;

err_i2s:
    if (ctx->tx != NULL) {
        i2s_del_channel(ctx->tx);
        ctx->tx = NULL;
    }
    if (ctx->rx != NULL) {
        i2s_del_channel(ctx->rx);
        ctx->rx = NULL;
    }
    bsp_es8311_deinit(&ctx->codec);
    return dal_err_from_esp(e);
}

static dal_err_t audio_play(void *ctx_, const void *data, size_t len)
{
    bsp_audio_ctx_t *ctx = (bsp_audio_ctx_t *)ctx_;
    if (ctx == NULL || !ctx->inited || data == NULL) {
        return DAL_ERR_INVALID;
    }
    size_t written = 0;
    esp_err_t e = i2s_channel_write(ctx->tx, data, len, &written,
                                    AUDIO_I2S_TIMEOUT_MS);
    if (e != ESP_OK) {
        return dal_err_from_esp(e);
    }
    if (written < len) {
        return DAL_ERR_TIMEOUT;
    }
    return DAL_OK;
}

static dal_err_t audio_record(void *ctx_, void *data, size_t len)
{
    bsp_audio_ctx_t *ctx = (bsp_audio_ctx_t *)ctx_;
    if (ctx == NULL || !ctx->inited || data == NULL) {
        return DAL_ERR_INVALID;
    }
    size_t rd = 0;
    esp_err_t e = i2s_channel_read(ctx->rx, data, len, &rd,
                                   AUDIO_I2S_TIMEOUT_MS);
    if (e != ESP_OK) {
        return dal_err_from_esp(e);
    }
    if (rd < len) {
        return DAL_ERR_TIMEOUT;
    }
    return DAL_OK;
}

static dal_err_t audio_set_volume(void *ctx_, uint8_t percent)
{
    bsp_audio_ctx_t *ctx = (bsp_audio_ctx_t *)ctx_;
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }
    return bsp_es8311_set_volume(&ctx->codec, percent);
}

static dal_err_t audio_set_mute(void *ctx_, bool mute)
{
    bsp_audio_ctx_t *ctx = (bsp_audio_ctx_t *)ctx_;
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }
    return bsp_es8311_set_mute(&ctx->codec, mute);
}

static bool audio_is_busy(void *ctx_)
{
    /* I2S channel_write/read 为阻塞式，无独立 busy 标志；此处返回 false（非实时精确） */
    (void)ctx_;
    return false;
}

static dal_err_t audio_deinit(void *ctx_)
{
    bsp_audio_ctx_t *ctx = (bsp_audio_ctx_t *)ctx_;
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }
    if ((int)ctx->pa_pin >= 0) {
        gpio_set_level(ctx->pa_pin, 0);
    }
    if (ctx->tx != NULL) {
        i2s_channel_disable(ctx->tx);
        i2s_del_channel(ctx->tx);
        ctx->tx = NULL;
    }
    if (ctx->rx != NULL) {
        i2s_channel_disable(ctx->rx);
        i2s_del_channel(ctx->rx);
        ctx->rx = NULL;
    }
    bsp_es8311_deinit(&ctx->codec);
    ctx->inited = false;
    return DAL_OK;
}

/* ================================================================
 *  静态 ops + ctx（单实例，ctx 编译期注入 ops->ctx）
 * ================================================================ */
static dal_audio_ops_t s_audio_ops = {
    .init       = audio_init,
    .play       = audio_play,
    .record     = audio_record,
    .set_volume = audio_set_volume,
    .set_mute   = audio_set_mute,
    .is_busy    = audio_is_busy,
    .deinit     = audio_deinit,
    .ctx        = &s_ctx,
};

/* ================================================================
 *  对外 create 入口（仅返回 ops 指针，不注册、不初始化硬件）
 * ================================================================ */
dal_audio_ops_t *bsp_audio_es8311_create(void)
{
    /* 静态 ops 编译期已注入 ctx；此处仅做非硬件的 ctx 字段清零 */
    memset(&s_ctx, 0, sizeof(s_ctx));
    return &s_audio_ops;
}
