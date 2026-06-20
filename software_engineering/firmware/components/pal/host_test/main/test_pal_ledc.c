/**
 * @file    test_pal_ledc.c
 * @brief   pal_ledc 模块 mock 单元测试
 *
 * pal_ledc 为无状态透传封装，但维护全局 g_duty_resolution[]（timer_config 成功时记录）。
 * 测试 set_duty_pct 时选用未被配置过的 channel 以走默认 13bit 分辨率路径，避免全局污染。
 */

#include "unity.h"
#include "unity_fixture.h"

#include "esp_err.h"
#include "Mockledc.h"

#include "pal_ledc.h"

TEST_GROUP(pal_ledc);

TEST_SETUP(pal_ledc)
{
    Mockledc_Init();
}

TEST_TEAR_DOWN(pal_ledc)
{
    Mockledc_Verify();
    Mockledc_Destroy();
}

/* ================================================================
 *  定时器 / 通道配置
 * ================================================================ */

TEST(pal_ledc, timer_config_calls_underlying)
{
    ledc_timer_config_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_ledc_timer_config(0, 5000, 13));
}

TEST(pal_ledc, timer_config_propagates_error)
{
    ledc_timer_config_ExpectAnyArgsAndReturn(ESP_FAIL);
    TEST_ASSERT_EQUAL_INT(ESP_FAIL, pal_ledc_timer_config(1, 1000, 10));
}

TEST(pal_ledc, channel_config_calls_underlying)
{
    ledc_channel_config_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_ledc_channel_config(0, 5, 0));
}

/* ================================================================
 *  占空比
 * ================================================================ */

TEST(pal_ledc, set_duty_calls_set_then_update)
{
    ledc_set_duty_ExpectAndReturn(LEDC_LOW_SPEED_MODE, (ledc_channel_t)0, 512, ESP_OK);
    ledc_update_duty_ExpectAndReturn(LEDC_LOW_SPEED_MODE, (ledc_channel_t)0, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_ledc_set_duty(0, 512));
}

TEST(pal_ledc, set_duty_propagates_set_error)
{
    ledc_set_duty_ExpectAndReturn(LEDC_LOW_SPEED_MODE, (ledc_channel_t)0, 512, ESP_FAIL);
    TEST_ASSERT_EQUAL_INT(ESP_FAIL, pal_ledc_set_duty(0, 512));
}

TEST(pal_ledc, set_duty_pct_uses_default_resolution)
{
    /* channel 7 未被 timer_config 配置过，走默认 13bit：max=8191，50% → 4095 */
    uint32_t expected_duty = ((uint32_t)1 << 13) - 1;
    expected_duty = (uint32_t)((uint64_t)expected_duty * 50 / 100);

    ledc_set_duty_ExpectAndReturn(LEDC_LOW_SPEED_MODE, (ledc_channel_t)7, expected_duty, ESP_OK);
    ledc_update_duty_ExpectAndReturn(LEDC_LOW_SPEED_MODE, (ledc_channel_t)7, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_ledc_set_duty_pct(7, 50));
}

TEST(pal_ledc, update_duty_passthrough)
{
    ledc_update_duty_ExpectAndReturn(LEDC_LOW_SPEED_MODE, (ledc_channel_t)0, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_ledc_update_duty(0));
}

/* ================================================================
 *  渐变
 * ================================================================ */

TEST(pal_ledc, fade_start_installs_then_starts)
{
    ledc_fade_func_install_ExpectAndReturn(0, ESP_OK);
    ledc_set_fade_time_and_start_ExpectAndReturn(LEDC_LOW_SPEED_MODE, (ledc_channel_t)0,
                                                  4095, 1000, LEDC_FADE_NO_WAIT, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_ledc_fade_start(0, 4095, 1000));
}

TEST(pal_ledc, fade_start_propagates_install_error)
{
    ledc_fade_func_install_ExpectAndReturn(0, ESP_ERR_NO_MEM);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NO_MEM, pal_ledc_fade_start(0, 4095, 1000));
}

TEST(pal_ledc, fade_stop_passthrough)
{
    ledc_fade_stop_ExpectAndReturn(LEDC_LOW_SPEED_MODE, (ledc_channel_t)0, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_ledc_fade_stop(0));
}

/* ================================================================
 *  停止 / 复位
 * ================================================================ */

TEST(pal_ledc, channel_stop_passthrough)
{
    ledc_stop_ExpectAndReturn(LEDC_LOW_SPEED_MODE, (ledc_channel_t)0, 0, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_ledc_channel_stop(0, 0));
}

TEST(pal_ledc, timer_reset_passthrough)
{
    ledc_timer_rst_ExpectAndReturn(LEDC_LOW_SPEED_MODE, (ledc_timer_t)0, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_ledc_timer_reset(0));
}

TEST_GROUP_RUNNER(pal_ledc)
{
    RUN_TEST_CASE(pal_ledc, timer_config_calls_underlying);
    RUN_TEST_CASE(pal_ledc, timer_config_propagates_error);
    RUN_TEST_CASE(pal_ledc, channel_config_calls_underlying);
    RUN_TEST_CASE(pal_ledc, set_duty_calls_set_then_update);
    RUN_TEST_CASE(pal_ledc, set_duty_propagates_set_error);
    RUN_TEST_CASE(pal_ledc, set_duty_pct_uses_default_resolution);
    RUN_TEST_CASE(pal_ledc, update_duty_passthrough);
    RUN_TEST_CASE(pal_ledc, fade_start_installs_then_starts);
    RUN_TEST_CASE(pal_ledc, fade_start_propagates_install_error);
    RUN_TEST_CASE(pal_ledc, fade_stop_passthrough);
    RUN_TEST_CASE(pal_ledc, channel_stop_passthrough);
    RUN_TEST_CASE(pal_ledc, timer_reset_passthrough);
}

void runner_pal_ledc(void)
{
    RUN_TEST_GROUP(pal_ledc);
}
