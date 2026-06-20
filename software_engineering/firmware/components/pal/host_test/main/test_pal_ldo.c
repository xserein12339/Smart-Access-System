/**
 * @file    test_pal_ldo.c
 * @brief   pal_ldo 模块 mock 单元测试
 *
 * 通过 CMock 生成的 Mockesp_ldo_regulator 验证 pal_ldo 对底层 LDO 驱动的
 * 调用契约、参数校验与错误码透传。
 *
 * mock 模式：TEST_SETUP 调 Init，TEST_TEAR_DOWN 调 Verify+Destroy；
 * 每个用例须为其触发的所有底层调用设置 Expect/Ignore。
 */

#include "unity.h"
#include "unity_fixture.h"

#include "esp_err.h"
#include "Mockesp_ldo_regulator.h"

#include "pal_ldo.h"

/* ================================================================
 *  测试组 / setup / teardown
 * ================================================================ */

TEST_GROUP(pal_ldo);

TEST_SETUP(pal_ldo)
{
    Mockesp_ldo_regulator_Init();
}

TEST_TEAR_DOWN(pal_ldo)
{
    Mockesp_ldo_regulator_Verify();
    Mockesp_ldo_regulator_Destroy();
}

/* ================================================================
 *  辅助：acquire 一个可用句柄（已设好 acquire 的 Expect）
 * ================================================================ */
static pal_ldo_handle_t acquire_handle(esp_ldo_channel_handle_t mock_chan)
{
    pal_ldo_handle_t handle = NULL;
    pal_ldo_config_t cfg = { .chan_id = 1, .voltage_mv = 2500 };

    esp_ldo_acquire_channel_ExpectAndReturn(NULL, NULL, ESP_OK);
    esp_ldo_acquire_channel_IgnoreArg_config();
    esp_ldo_acquire_channel_IgnoreArg_out_handle();
    esp_ldo_acquire_channel_ReturnThruPtr_out_handle(&mock_chan);

    pal_ldo_acquire(&handle, &cfg);
    return handle;
}

/* ================================================================
 *  pal_ldo_acquire
 * ================================================================ */

TEST(pal_ldo, acquire_success_returns_handle)
{
    pal_ldo_handle_t handle = NULL;
    pal_ldo_config_t cfg = { .chan_id = 3, .voltage_mv = 2500 };

    esp_ldo_channel_handle_t mock_chan = (esp_ldo_channel_handle_t)0xABCD;
    esp_ldo_acquire_channel_ExpectAndReturn(NULL, NULL, ESP_OK);
    esp_ldo_acquire_channel_IgnoreArg_config();
    esp_ldo_acquire_channel_IgnoreArg_out_handle();
    esp_ldo_acquire_channel_ReturnThruPtr_out_handle(&mock_chan);

    int ret = pal_ldo_acquire(&handle, &cfg);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_NOT_NULL(handle);

    /* 释放以避免泄漏，需为 release_channel 设 Expect */
    esp_ldo_release_channel_ExpectAndReturn(mock_chan, ESP_OK);
    pal_ldo_release(handle);
}

TEST(pal_ldo, acquire_driver_failure_propagates_error)
{
    pal_ldo_handle_t handle = NULL;
    pal_ldo_config_t cfg = { .chan_id = 1, .voltage_mv = 1800 };

    esp_ldo_acquire_channel_ExpectAndReturn(NULL, NULL, ESP_ERR_INVALID_STATE);
    esp_ldo_acquire_channel_IgnoreArg_config();
    esp_ldo_acquire_channel_IgnoreArg_out_handle();

    int ret = pal_ldo_acquire(&handle, &cfg);

    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, ret);
    TEST_ASSERT_NULL(handle);
}

TEST(pal_ldo, acquire_null_args_returns_invalid_arg)
{
    pal_ldo_handle_t handle = NULL;
    pal_ldo_config_t cfg = { .chan_id = 1, .voltage_mv = 3300 };

    /* 不应触发底层调用 */
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_ldo_acquire(NULL, &cfg));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_ldo_acquire(&handle, NULL));
    TEST_ASSERT_NULL(handle);
}

/* ================================================================
 *  pal_ldo_release
 * ================================================================ */

TEST(pal_ldo, release_calls_underlying_release)
{
    esp_ldo_channel_handle_t mock_chan = (esp_ldo_channel_handle_t)0x1234;
    pal_ldo_handle_t handle = acquire_handle(mock_chan);

    esp_ldo_release_channel_ExpectAndReturn(mock_chan, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_ldo_release(handle));
}

TEST(pal_ldo, release_null_returns_invalid_arg)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_ldo_release(NULL));
}

/* ================================================================
 *  pal_ldo_set_voltage
 * ================================================================ */

TEST(pal_ldo, set_voltage_calls_adjust)
{
    esp_ldo_channel_handle_t mock_chan = (esp_ldo_channel_handle_t)0x5678;
    pal_ldo_handle_t handle = acquire_handle(mock_chan);

    esp_ldo_channel_adjust_voltage_ExpectAndReturn(mock_chan, 3300, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_ldo_set_voltage(handle, 3300));

    esp_ldo_release_channel_ExpectAndReturn(mock_chan, ESP_OK);
    pal_ldo_release(handle);
}

TEST(pal_ldo, set_voltage_null_returns_invalid_arg)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_ldo_set_voltage(NULL, 3300));
}

/* ================================================================
 *  pal_ldo_get_handle
 * ================================================================ */

TEST(pal_ldo, get_handle_returns_underlying)
{
    esp_ldo_channel_handle_t mock_chan = (esp_ldo_channel_handle_t)0xBEEF;
    pal_ldo_handle_t handle = acquire_handle(mock_chan);

    TEST_ASSERT_EQUAL_PTR(mock_chan, pal_ldo_get_handle(handle));

    esp_ldo_release_channel_ExpectAndReturn(mock_chan, ESP_OK);
    pal_ldo_release(handle);
}

TEST(pal_ldo, get_handle_null_returns_null)
{
    TEST_ASSERT_NULL(pal_ldo_get_handle(NULL));
}

/* ================================================================
 *  Group runner
 * ================================================================ */

TEST_GROUP_RUNNER(pal_ldo)
{
    RUN_TEST_CASE(pal_ldo, acquire_success_returns_handle);
    RUN_TEST_CASE(pal_ldo, acquire_driver_failure_propagates_error);
    RUN_TEST_CASE(pal_ldo, acquire_null_args_returns_invalid_arg);
    RUN_TEST_CASE(pal_ldo, release_calls_underlying_release);
    RUN_TEST_CASE(pal_ldo, release_null_returns_invalid_arg);
    RUN_TEST_CASE(pal_ldo, set_voltage_calls_adjust);
    RUN_TEST_CASE(pal_ldo, set_voltage_null_returns_invalid_arg);
    RUN_TEST_CASE(pal_ldo, get_handle_returns_underlying);
    RUN_TEST_CASE(pal_ldo, get_handle_null_returns_null);
}

void runner_pal_ldo(void)
{
    RUN_TEST_GROUP(pal_ldo);
}
