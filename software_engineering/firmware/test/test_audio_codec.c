/**
 * @file    test_audio_codec.c
 * @brief   ES8311 codec 子驱动单元测试（宿主机 Mock 测试）
 *
 * @details 验证 ES8311 寄存器初始化序列与音量/静音控制：
 *            - init 写复位/I2S slave/外部 MCLK/16-bit Philips/启用 DAC 序列
 *            - set_volume 百分比映射到 0x00~0xC0
 *            - set_mute 写 DAC31 (0x60 静音 / 0x00 取消)
 *          pal_i2c_write 用 custom fake 记录 (reg,val) 序列。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "fff.h"
#include "unity.h"

/* ---- Mock 头 ---- */
#include "esp_err.h"
#include "pal_i2c.h"
#include "pal_log.h"
#include "osal_task.h"

/* ---- 被测模块 ---- */
#include "bsp_audio_es8311_codec.h"
#include "bsp_config.h"

/* ================================================================
 *  custom fake：记录 pal_i2c_write 的 (reg, val)
 * ================================================================ */
#define MAX_WRITE_RECORDS 128
typedef struct { uint8_t reg; uint8_t val; } write_record_t;
static write_record_t s_writes[MAX_WRITE_RECORDS];
static int            s_write_count;
static uint16_t       s_last_attach_addr;

static int attach_sets_dev(pal_i2c_dev_handle_t *dev, pal_i2c_bus_handle_t bus,
                           const pal_i2c_dev_config_t *cfg)
{
    (void)bus;
    if (dev) *dev = (pal_i2c_dev_handle_t)1;
    if (cfg) s_last_attach_addr = cfg->device_address;
    return 0;
}

static int write_recorder(pal_i2c_dev_handle_t dev, const uint8_t *data, size_t len)
{
    (void)dev;
    if (data != NULL && len >= 2 && s_write_count < MAX_WRITE_RECORDS) {
        s_writes[s_write_count].reg = data[0];
        s_writes[s_write_count].val = data[1];
        s_write_count++;
    }
    return 0;
}

static int find_last_write(uint8_t reg)
{
    for (int i = s_write_count - 1; i >= 0; i--) {
        if (s_writes[i].reg == reg) return s_writes[i].val;
    }
    return -1;
}

/** 检查序列中是否曾写入 (reg, val) */
static bool ever_wrote(uint8_t reg, uint8_t val)
{
    for (int i = 0; i < s_write_count; i++) {
        if (s_writes[i].reg == reg && s_writes[i].val == val) return true;
    }
    return false;
}

/* ================================================================
 *  setUp / tearDown
 * ================================================================ */
static bsp_es8311_ctx_t s_ctx;

void setUp(void)
{
    RESET_FAKE(pal_i2c_dev_attach);
    RESET_FAKE(pal_i2c_dev_detach);
    RESET_FAKE(pal_i2c_write);

    memset(s_writes, 0, sizeof(s_writes));
    s_write_count = 0;
    s_last_attach_addr = 0;
    memset(&s_ctx, 0, sizeof(s_ctx));

    pal_i2c_dev_attach_fake.return_val = 0;
    pal_i2c_dev_attach_fake.custom_fake = attach_sets_dev;
    pal_i2c_dev_detach_fake.return_val = 0;
    pal_i2c_write_fake.custom_fake     = write_recorder;
}

void tearDown(void) {}

/* ================================================================
 *  init 寄存器序列
 * ================================================================ */
void test_es8311_init_writes_reset_slave_mode(void)
{
    pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)1;
    TEST_ASSERT_EQUAL(DAL_OK, bsp_es8311_init(&s_ctx, bus));
    /* RESET = 0x80（slave mode）应在序列中出现 */
    TEST_ASSERT_TRUE(ever_wrote(ES8311_REG_RESET, 0x80));
}

void test_es8311_init_uses_external_mclk(void)
{
    bsp_es8311_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    /* CLK_MGR1 最终写入 0x3F（外部 MCLK） */
    TEST_ASSERT_EQUAL(0x3F, find_last_write(ES8311_REG_CLK_MGR1));
}

void test_es8311_init_sets_16bit_philips_sdp(void)
{
    bsp_es8311_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    /* SDP_IN / SDP_OUT = 0x0C（16-bit I2S Philips） */
    TEST_ASSERT_EQUAL(0x0C, find_last_write(ES8311_REG_SDP_IN));
    TEST_ASSERT_EQUAL(0x0C, find_last_write(ES8311_REG_SDP_OUT));
}

void test_es8311_init_enables_dac_and_max_volume(void)
{
    bsp_es8311_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    /* DAC31 = 0x00（取消静音） */
    TEST_ASSERT_EQUAL(0x00, find_last_write(ES8311_REG_DAC31));
    /* VOLUME = 0xC0（最大） */
    TEST_ASSERT_EQUAL(ES8311_VOLUME_MAX, find_last_write(ES8311_REG_VOLUME));
    TEST_ASSERT_TRUE(s_ctx.inited);
}

void test_es8311_init_attaches_i2c_at_board_addr(void)
{
    bsp_es8311_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    TEST_ASSERT_EQUAL(1, pal_i2c_dev_attach_fake.call_count);
    TEST_ASSERT_EQUAL(BOARD_AUDIO_CODEC_I2C_ADDR, s_last_attach_addr);
}

void test_es8311_init_null_args_should_fail(void)
{
    pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)1;
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, bsp_es8311_init(NULL, bus));
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, bsp_es8311_init(&s_ctx, NULL));
}

void test_es8311_init_attach_fail_returns_error(void)
{
    pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)1;
    pal_i2c_dev_attach_fake.custom_fake = NULL;
    pal_i2c_dev_attach_fake.return_val = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, bsp_es8311_init(&s_ctx, bus));
    TEST_ASSERT_FALSE(s_ctx.inited);
}

/* ================================================================
 *  set_volume 映射
 * ================================================================ */
void test_es8311_set_volume_100_maps_to_max(void)
{
    bsp_es8311_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    s_write_count = 0;
    TEST_ASSERT_EQUAL(DAL_OK, bsp_es8311_set_volume(&s_ctx, 100));
    TEST_ASSERT_EQUAL(0xC0, find_last_write(ES8311_REG_VOLUME));
}

void test_es8311_set_volume_0_maps_to_zero(void)
{
    bsp_es8311_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    s_write_count = 0;
    TEST_ASSERT_EQUAL(DAL_OK, bsp_es8311_set_volume(&s_ctx, 0));
    TEST_ASSERT_EQUAL(0x00, find_last_write(ES8311_REG_VOLUME));
}

void test_es8311_set_volume_not_inited_should_fail(void)
{
    TEST_ASSERT_EQUAL(DAL_ERR_INVALID, bsp_es8311_set_volume(&s_ctx, 50));
}

/* ================================================================
 *  set_mute
 * ================================================================ */
void test_es8311_set_mute_true_writes_0x60(void)
{
    bsp_es8311_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    s_write_count = 0;
    TEST_ASSERT_EQUAL(DAL_OK, bsp_es8311_set_mute(&s_ctx, true));
    TEST_ASSERT_EQUAL(0x60, find_last_write(ES8311_REG_DAC31));
}

void test_es8311_set_mute_false_writes_0x00(void)
{
    bsp_es8311_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    s_write_count = 0;
    TEST_ASSERT_EQUAL(DAL_OK, bsp_es8311_set_mute(&s_ctx, false));
    TEST_ASSERT_EQUAL(0x00, find_last_write(ES8311_REG_DAC31));
}

/* ================================================================
 *  deinit
 * ================================================================ */
void test_es8311_deinit_resets_and_detaches(void)
{
    bsp_es8311_init(&s_ctx, (pal_i2c_bus_handle_t)1);
    s_write_count = 0;
    TEST_ASSERT_EQUAL(DAL_OK, bsp_es8311_deinit(&s_ctx));
    /* deinit 写 RESET=0x00 */
    TEST_ASSERT_EQUAL(0x00, find_last_write(ES8311_REG_RESET));
    TEST_ASSERT_EQUAL(1, pal_i2c_dev_detach_fake.call_count);
    TEST_ASSERT_FALSE(s_ctx.inited);
}

/* ================================================================
 *  测试运行入口
 * ================================================================ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_es8311_init_writes_reset_slave_mode);
    RUN_TEST(test_es8311_init_uses_external_mclk);
    RUN_TEST(test_es8311_init_sets_16bit_philips_sdp);
    RUN_TEST(test_es8311_init_enables_dac_and_max_volume);
    RUN_TEST(test_es8311_init_attaches_i2c_at_board_addr);
    RUN_TEST(test_es8311_init_null_args_should_fail);
    RUN_TEST(test_es8311_init_attach_fail_returns_error);

    RUN_TEST(test_es8311_set_volume_100_maps_to_max);
    RUN_TEST(test_es8311_set_volume_0_maps_to_zero);
    RUN_TEST(test_es8311_set_volume_not_inited_should_fail);

    RUN_TEST(test_es8311_set_mute_true_writes_0x60);
    RUN_TEST(test_es8311_set_mute_false_writes_0x00);

    RUN_TEST(test_es8311_deinit_resets_and_detaches);

    return UNITY_END();
}
