/**
 * @file    test_pal_i2s.c
 * @brief   pal_i2s 模块 mock 单元测试
 *
 * pal_i2s 维护内部结构体 pal_i2s_internal_t {tx, rx}，按方向创建 tx/rx 通道。
 * 通过 Mocki2s_std + Mocki2s_common 验证生命周期、调用契约与错误码透传。
 */

#include "unity.h"
#include "unity_fixture.h"

#include "esp_err.h"
#include "Mocki2s_std.h"
#include "Mocki2s_common.h"

#include "pal_i2s.h"

TEST_GROUP(pal_i2s);

TEST_SETUP(pal_i2s)
{
    Mocki2s_std_Init();
    Mocki2s_common_Init();
}

TEST_TEAR_DOWN(pal_i2s)
{
    Mocki2s_std_Verify();
    Mocki2s_std_Destroy();
    Mocki2s_common_Verify();
    Mocki2s_common_Destroy();
}

/* ================================================================
 *  辅助
 * ================================================================ */

static pal_i2s_config_t tx_cfg(void)
{
    pal_i2s_config_t cfg = {
        .port = 0, .role = PAL_I2S_ROLE_MASTER, .dir = PAL_I2S_DIR_TX,
        .mclk_pin = -1, .bclk_pin = 4, .ws_pin = 5, .dout_pin = 6, .din_pin = -1,
        .sample_rate_hz = 48000, .data_bit_width = PAL_I2S_DATA_16BIT,
        .slot_mode = PAL_I2S_SLOT_STEREO, .dma_desc_num = 6, .dma_frame_num = 240,
        .intr_priority = 0,
    };
    return cfg;
}

/* ================================================================
 *  生命周期
 * ================================================================ */

TEST(pal_i2s, init_tx_success)
{
    pal_i2s_handle_t handle = NULL;
    pal_i2s_config_t cfg = tx_cfg();
    i2s_chan_handle_t mock_tx = (i2s_chan_handle_t)0xC000;

    /* dir=TX：new_channel 第二参数返回 tx，第三参数 NULL */
    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_new_channel_ReturnThruPtr_ret_tx_handle(&mock_tx);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);

    TEST_ASSERT_EQUAL_INT(0, pal_i2s_init(&handle, &cfg));
    TEST_ASSERT_NOT_NULL(handle);

    /* 清理：deinit 会 disable + del tx */
    i2s_channel_disable_ExpectAndReturn(mock_tx, ESP_OK);
    i2s_del_channel_ExpectAndReturn(mock_tx, ESP_OK);
    pal_i2s_deinit(handle);
}

TEST(pal_i2s, init_null_args)
{
    pal_i2s_handle_t h = NULL;
    pal_i2s_config_t cfg = tx_cfg();
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2s_init(NULL, &cfg));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2s_init(&h, NULL));
}

TEST(pal_i2s, init_new_channel_failure_frees_struct)
{
    pal_i2s_handle_t handle = NULL;
    pal_i2s_config_t cfg = tx_cfg();
    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_ERR_NO_MEM);
    /* new_channel 失败 → 直接 free，不调 init_std/del */
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NO_MEM, pal_i2s_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

TEST(pal_i2s, init_std_mode_failure_cleans_channel)
{
    pal_i2s_handle_t handle = NULL;
    pal_i2s_config_t cfg = tx_cfg();
    i2s_chan_handle_t mock_tx = (i2s_chan_handle_t)0xC000;

    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_new_channel_ReturnThruPtr_ret_tx_handle(&mock_tx);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_FAIL);
    /* init_std 失败 → err_del: del_channel(tx) + free */
    i2s_del_channel_ExpectAndReturn(mock_tx, ESP_OK);

    TEST_ASSERT_EQUAL_INT(ESP_FAIL, pal_i2s_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

TEST(pal_i2s, deinit_null)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2s_deinit(NULL));
}

/* ================================================================
 *  通道控制
 * ================================================================ */

TEST(pal_i2s, enable_calls_channel_enable)
{
    pal_i2s_handle_t handle = NULL;
    pal_i2s_config_t cfg = tx_cfg();
    i2s_chan_handle_t mock_tx = (i2s_chan_handle_t)0xC000;
    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_new_channel_ReturnThruPtr_ret_tx_handle(&mock_tx);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    pal_i2s_init(&handle, &cfg);

    i2s_channel_enable_ExpectAndReturn(mock_tx, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_i2s_enable(handle));

    i2s_channel_disable_ExpectAndReturn(mock_tx, ESP_OK);
    i2s_del_channel_ExpectAndReturn(mock_tx, ESP_OK);
    pal_i2s_deinit(handle);
}

TEST(pal_i2s, disable_null)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2s_disable(NULL));
}

/* ================================================================
 *  数据收发
 * ================================================================ */

TEST(pal_i2s, write_calls_channel_write)
{
    pal_i2s_handle_t handle = NULL;
    pal_i2s_config_t cfg = tx_cfg();
    i2s_chan_handle_t mock_tx = (i2s_chan_handle_t)0xC000;
    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_new_channel_ReturnThruPtr_ret_tx_handle(&mock_tx);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    pal_i2s_init(&handle, &cfg);

    uint8_t data[4] = {0};
    i2s_channel_write_ExpectAndReturn(mock_tx, data, 4, NULL, 100, ESP_OK);
    i2s_channel_write_IgnoreArg_bytes_written();
    TEST_ASSERT_EQUAL_INT(0, pal_i2s_write(handle, data, 4, 100, NULL));

    i2s_channel_disable_ExpectAndReturn(mock_tx, ESP_OK);
    i2s_del_channel_ExpectAndReturn(mock_tx, ESP_OK);
    pal_i2s_deinit(handle);
}

TEST(pal_i2s, write_null_args)
{
    pal_i2s_handle_t handle = NULL;
    pal_i2s_config_t cfg = tx_cfg();
    i2s_chan_handle_t mock_tx = (i2s_chan_handle_t)0xC000;
    i2s_new_channel_ExpectAnyArgsAndReturn(ESP_OK);
    i2s_new_channel_ReturnThruPtr_ret_tx_handle(&mock_tx);
    i2s_channel_init_std_mode_ExpectAnyArgsAndReturn(ESP_OK);
    pal_i2s_init(&handle, &cfg);

    uint8_t data[4] = {0};
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2s_write(NULL, data, 4, 100, NULL));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2s_write(handle, NULL, 4, 100, NULL));

    i2s_channel_disable_ExpectAndReturn(mock_tx, ESP_OK);
    i2s_del_channel_ExpectAndReturn(mock_tx, ESP_OK);
    pal_i2s_deinit(handle);
}

TEST_GROUP_RUNNER(pal_i2s)
{
    RUN_TEST_CASE(pal_i2s, init_tx_success);
    RUN_TEST_CASE(pal_i2s, init_null_args);
    RUN_TEST_CASE(pal_i2s, init_new_channel_failure_frees_struct);
    RUN_TEST_CASE(pal_i2s, init_std_mode_failure_cleans_channel);
    RUN_TEST_CASE(pal_i2s, deinit_null);
    RUN_TEST_CASE(pal_i2s, enable_calls_channel_enable);
    RUN_TEST_CASE(pal_i2s, disable_null);
    RUN_TEST_CASE(pal_i2s, write_calls_channel_write);
    RUN_TEST_CASE(pal_i2s, write_null_args);
}

void runner_pal_i2s(void)
{
    RUN_TEST_GROUP(pal_i2s);
}
