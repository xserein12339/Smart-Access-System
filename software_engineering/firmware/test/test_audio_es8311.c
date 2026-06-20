/**
 * @file    test_audio_es8311.c
 * @brief   ES8311 音频 BSP 组装器单元测试（宿主机 Mock 测试）
 *
 * @details 验证 audio 组装器对 codec + I2S + PA 的编排：
 *            - init 初始化 codec（I2C）+ I2S + 使能 PA
 *            - play 分发到 pal_i2s_write，bytes_written 不足返回 TIMEOUT
 *            - record 分发到 pal_i2s_read
 *            - set_volume/set_mute 分发到 codec 寄存器
 *          pal_i2c_write 用 custom fake 记录 codec 寄存器；
 *          pal_i2s_write/read 用 custom fake 设 bytes_written。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "fff.h"
#include "unity.h"

/* ---- Mock 头 ---- */
#include "esp_err.h"
#include "pal_i2c.h"
#include "pal_i2s.h"
#include "pal_gpio.h"
#include "pal_log.h"
#include "osal_task.h"
#include "board_v1.h"

/* ---- 被测模块 ---- */
#include "bsp_audio_es8311.h"
#include "bsp_audio_es8311_codec.h"
#include "dal_audio.h"
#include "dal_audio_interface.h"
#include "dal_err.h"
#include "bsp_config.h"

/* ================================================================
 *  custom fake
 * ================================================================ */
/* codec 寄存器写入记录（与 test_audio_codec 同模式） */
#define MAX_WR 128
static struct { uint8_t reg, val; } s_wr[MAX_WR];
static int s_wr_count;
static int i2c_write_recorder(pal_i2c_dev_handle_t d, const uint8_t *data, size_t len)
{
    (void)d;
    if (data && len >= 2 && s_wr_count < MAX_WR) {
        s_wr[s_wr_count].reg = data[0];
        s_wr[s_wr_count].val = data[1];
        s_wr_count++;
    }
    return 0;
}
static int last_wr(uint8_t reg)
{
    for (int i = s_wr_count - 1; i >= 0; i--)
        if (s_wr[i].reg == reg) return s_wr[i].val;
    return -1;
}

static int attach_sets_dev(pal_i2c_dev_handle_t *dev, pal_i2c_bus_handle_t bus,
                           const pal_i2c_dev_config_t *cfg)
{
    (void)bus; (void)cfg;
    if (dev) *dev = (pal_i2c_dev_handle_t)1;
    return 0;
}

/* 记录 i2s_init 的 config（cfg 为调用方栈变量，勿存指针） */
static uint32_t s_i2s_sample_rate;
static int i2s_init_recorder(pal_i2s_handle_t *handle, const pal_i2s_config_t *cfg)
{
    if (handle) *handle = (pal_i2s_handle_t)1;
    if (cfg) s_i2s_sample_rate = cfg->sample_rate_hz;
    return 0;
}

/* I2S write fake：设 bytes_written = len（默认全写成功） */
static int s_i2s_write_bytes;   /* <0 表示部分写 */
static int i2s_write_fake_fn(pal_i2s_handle_t h, const void *src, size_t len,
                             uint32_t timeout, size_t *written)
{
    (void)h; (void)src; (void)timeout;
    if (written) {
        *written = (s_i2s_write_bytes < 0) ? 0 : len;
    }
    return 0;
}
static int i2s_read_fake_fn(pal_i2s_handle_t h, void *dst, size_t len,
                            uint32_t timeout, size_t *rd)
{
    (void)h; (void)dst; (void)len; (void)timeout;
    if (rd) *rd = len;
    return 0;
}

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */
static bool s_inited = false;
static const dal_audio_ops_t *s_ops = NULL;
static void                  *s_ctx = NULL;

void setUp(void)
{
    RESET_FAKE(pal_i2c_dev_attach);
    RESET_FAKE(pal_i2c_dev_detach);
    RESET_FAKE(pal_i2c_write);
    RESET_FAKE(pal_i2s_init);
    RESET_FAKE(pal_i2s_deinit);
    RESET_FAKE(pal_i2s_enable);
    RESET_FAKE(pal_i2s_disable);
    RESET_FAKE(pal_i2s_write);
    RESET_FAKE(pal_i2s_read);
    RESET_FAKE(pal_gpio_set_direction);
    RESET_FAKE(pal_gpio_write);

    memset(s_wr, 0, sizeof(s_wr));
    s_wr_count = 0;
    s_i2s_write_bytes = 0;

    pal_i2c_dev_attach_fake.return_val     = 0;
    pal_i2c_dev_attach_fake.custom_fake    = attach_sets_dev;
    pal_i2c_dev_detach_fake.return_val     = 0;
    pal_i2c_write_fake.custom_fake         = i2c_write_recorder;
    pal_i2s_init_fake.return_val           = 0;
    pal_i2s_init_fake.custom_fake          = i2s_init_recorder;
    pal_i2s_deinit_fake.return_val         = 0;
    pal_i2s_enable_fake.return_val         = 0;
    pal_i2s_disable_fake.return_val        = 0;
    pal_i2s_write_fake.custom_fake         = i2s_write_fake_fn;
    pal_i2s_read_fake.custom_fake          = i2s_read_fake_fn;
    pal_gpio_set_direction_fake.return_val = 0;
    pal_gpio_write_fake.return_val         = 0;

    if (!s_inited) {
        TEST_ASSERT_EQUAL(DAL_OK, bsp_audio_es8311_init());
        TEST_ASSERT_EQUAL(DAL_OK, dal_audio_get("main_audio", &s_ops, &s_ctx));
        s_inited = true;
    }
}

void tearDown(void) {}

/* ================================================================
 *  init 编排
 * ================================================================ */
void test_audio_init_initializes_codec_i2s_and_pa(void)
{
    /* init 已在 setUp 执行 */
    TEST_ASSERT_EQUAL(1, pal_i2s_init_fake.call_count);
    TEST_ASSERT_EQUAL(1, pal_i2s_enable_fake.call_count);
    /* PA 使能：pal_gpio_write(PA_PIN, 1) */
    TEST_ASSERT_EQUAL(1, pal_gpio_set_direction_fake.call_count);
    TEST_ASSERT_EQUAL(BOARD_AUDIO_PA_EN_PIN, pal_gpio_set_direction_fake.arg0_val);
    /* 至少一次写 PA 高电平（init 中） */
    bool pa_high_seen = false;
    for (uint32_t i = 0; i < pal_gpio_write_fake.call_count; i++) {
        if (pal_gpio_write_fake.arg0_history[i] == BOARD_AUDIO_PA_EN_PIN &&
            pal_gpio_write_fake.arg1_history[i] == 1) {
            pa_high_seen = true;
        }
    }
    TEST_ASSERT_TRUE(pa_high_seen);
}

void test_audio_init_i2s_config_uses_board_sample_rate(void)
{
    /* pal_i2s_init 的 config 参数 sample_rate_hz 应为板级默认 */
    TEST_ASSERT_EQUAL(BOARD_AUDIO_SAMPLE_RATE, s_i2s_sample_rate);
}

/* ================================================================
 *  play
 * ================================================================ */
void test_audio_play_delegates_to_i2s_write(void)
{
    int16_t buf[4] = {0};
    TEST_ASSERT_EQUAL(DAL_OK, s_ops->play(s_ctx, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(1, pal_i2s_write_fake.call_count);
}

void test_audio_play_partial_write_returns_timeout(void)
{
    s_i2s_write_bytes = -1;   /* 触发 bytes_written=0 */
    int16_t buf[4] = {0};
    TEST_ASSERT_EQUAL(DAL_ERR_TIMEOUT, s_ops->play(s_ctx, buf, sizeof(buf)));
}

void test_audio_play_null_args_should_fail(void)
{
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, s_ops->play(s_ctx, NULL, 10));
}

/* ================================================================
 *  record
 * ================================================================ */
void test_audio_record_delegates_to_i2s_read(void)
{
    int16_t buf[4] = {0};
    TEST_ASSERT_EQUAL(DAL_OK, s_ops->record(s_ctx, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(1, pal_i2s_read_fake.call_count);
}

/* ================================================================
 *  set_volume / set_mute 分发到 codec
 * ================================================================ */
void test_audio_set_volume_writes_codec_volume_reg(void)
{
    s_wr_count = 0;
    TEST_ASSERT_EQUAL(DAL_OK, s_ops->set_volume(s_ctx, 100));
    TEST_ASSERT_EQUAL(0xC0, last_wr(ES8311_REG_VOLUME));
}

void test_audio_set_mute_writes_codec_dac31(void)
{
    s_wr_count = 0;
    TEST_ASSERT_EQUAL(DAL_OK, s_ops->set_mute(s_ctx, true));
    TEST_ASSERT_EQUAL(0x60, last_wr(ES8311_REG_DAC31));
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_audio_init_initializes_codec_i2s_and_pa);
    RUN_TEST(test_audio_init_i2s_config_uses_board_sample_rate);

    RUN_TEST(test_audio_play_delegates_to_i2s_write);
    RUN_TEST(test_audio_play_partial_write_returns_timeout);
    RUN_TEST(test_audio_play_null_args_should_fail);

    RUN_TEST(test_audio_record_delegates_to_i2s_read);

    RUN_TEST(test_audio_set_volume_writes_codec_volume_reg);
    RUN_TEST(test_audio_set_mute_writes_codec_dac31);

    return UNITY_END();
}
