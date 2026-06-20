/**
 * @file    bsp_audio_es8311.c
 * @brief   ES8311 音频 BSP 实现 — codec + I2S 组装，自注册到 DAL
 *
 * @details 组装 ES8311 codec（I2C 寄存器配置）与 PAL I2S（PCM 收发），
 *          实现 dal_audio_ops_t：
 *          - init：从 board 获取共享 I2C 总线初始化 codec；初始化 PAL I2S
 *            （TX/RX 全双工，参数来自 bsp_config.h）；使能 PA。
 *          - play：pal_i2s_write 写 PCM 数据。
 *          - record：pal_i2s_read 读 PCM 数据。
 *          - set_volume/set_mute：转 ES8311 寄存器。
 *          PAL 返回码经 dal_err_from_pal 翻译为 dal_err_t。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "bsp_audio_es8311.h"
#include "dal_audio.h"
#include "dal_audio_interface.h"
#include "dal_pal_err.h"
#include "bsp_audio_es8311_codec.h"
#include "bsp_config.h"
#include "pal_i2s.h"
#include "pal_i2c.h"
#include "pal_gpio.h"
#include "board_v1.h"
#include "pal_log.h"

/* ---- BSP 私有聚合上下文 ---- */
typedef struct {
    bsp_es8311_ctx_t  codec;        /**< ES8311 codec 上下文 */
    pal_i2s_handle_t  i2s;          /**< PAL I2S 句柄 */
    int               pa_pin;       /**< PA 使能引脚，-1 无 */
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
    pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)board_i2c_get_bus();
    if (bus == NULL) {
        PAL_LOGE("AUDIO", "shared I2C bus not ready");
        return DAL_ERR_STATE;
    }
    dal_err_t ret = bsp_es8311_init(&ctx->codec, bus);
    if (ret != DAL_OK) {
        return ret;
    }

    /* 2. 初始化 PAL I2S（全双工主机，16-bit 立体声） */
    pal_i2s_config_t i2s_cfg = {
        .port           = BOARD_AUDIO_I2S_PORT,
        .role           = PAL_I2S_ROLE_MASTER,
        .dir            = PAL_I2S_DIR_TX_RX,
        .mclk_pin       = BOARD_AUDIO_MCLK_PIN,
        .bclk_pin       = BOARD_AUDIO_BCLK_PIN,
        .ws_pin         = BOARD_AUDIO_LRCK_PIN,
        .dout_pin       = BOARD_AUDIO_DOUT_PIN,
        .din_pin        = BOARD_AUDIO_DIN_PIN,
        .sample_rate_hz = cfg->sample_rate_hz,
        .data_bit_width = PAL_I2S_DATA_16BIT,
        .slot_mode      = PAL_I2S_SLOT_STEREO,
        .dma_desc_num   = 6,
        .dma_frame_num  = 240,
        .intr_priority  = 0,
    };
    int pret = pal_i2s_init(&ctx->i2s, &i2s_cfg);
    if (pret != 0) {
        bsp_es8311_deinit(&ctx->codec);
        return dal_err_from_pal(pret);
    }
    pal_i2s_enable(ctx->i2s);

    /* 3. 使能 PA */
    ctx->pa_pin = BOARD_AUDIO_PA_EN_PIN;
    if (ctx->pa_pin >= 0) {
        pal_gpio_set_direction(ctx->pa_pin, PAL_GPIO_DIR_OUTPUT);
        pal_gpio_write(ctx->pa_pin, 1);
    }

    ctx->sample_rate = cfg->sample_rate_hz;
    ctx->inited      = true;

    /* 4. 应用初始音量 */
    bsp_es8311_set_volume(&ctx->codec, cfg->volume);

    PAL_LOGI("AUDIO", "initialized (%lu Hz)", (unsigned long)cfg->sample_rate_hz);
    return DAL_OK;
}

static dal_err_t audio_play(void *ctx_, const void *data, size_t len)
{
    bsp_audio_ctx_t *ctx = (bsp_audio_ctx_t *)ctx_;
    if (ctx == NULL || !ctx->inited || data == NULL) {
        return DAL_ERR_INVALID;
    }
    size_t written = 0;
    int ret = pal_i2s_write(ctx->i2s, data, len, AUDIO_I2S_TIMEOUT_MS, &written);
    if (ret != 0) {
        return dal_err_from_pal(ret);
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
    int ret = pal_i2s_read(ctx->i2s, data, len, AUDIO_I2S_TIMEOUT_MS, &rd);
    if (ret != 0) {
        return dal_err_from_pal(ret);
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
    /* PAL I2S 写为阻塞式，无独立 busy 标志；此处返回 false（非实时精确） */
    (void)ctx_;
    return false;
}

static dal_err_t audio_deinit(void *ctx_)
{
    bsp_audio_ctx_t *ctx = (bsp_audio_ctx_t *)ctx_;
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }
    if (ctx->pa_pin >= 0) {
        pal_gpio_write(ctx->pa_pin, 0);
    }
    if (ctx->i2s != NULL) {
        pal_i2s_disable(ctx->i2s);
        pal_i2s_deinit(ctx->i2s);
        ctx->i2s = NULL;
    }
    bsp_es8311_deinit(&ctx->codec);
    ctx->inited = false;
    return DAL_OK;
}

static const dal_audio_ops_t s_audio_ops = {
    .init       = audio_init,
    .play       = audio_play,
    .record     = audio_record,
    .set_volume = audio_set_volume,
    .set_mute   = audio_set_mute,
    .is_busy    = audio_is_busy,
    .deinit     = audio_deinit,
};

/* ================================================================
 *  对外初始化入口（自注册）
 * ================================================================ */
dal_err_t bsp_audio_es8311_init(void)
{
    /* 用板级默认采样率预初始化 */
    dal_audio_config_t cfg = {
        .sample_rate_hz = BOARD_AUDIO_SAMPLE_RATE,
        .volume         = 50,
    };
    dal_err_t ret = audio_init(&s_ctx, &cfg);
    if (ret != DAL_OK) {
        return ret;
    }

    /* 自注册到 DAL */
    return dal_audio_register("main_audio", &s_audio_ops, &s_ctx);
}
