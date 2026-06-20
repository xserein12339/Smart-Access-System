/**
 * @file    test_pal_wdt.c
 * @brief   pal_wdt 模块 mock 单元测试
 *
 * 通过 CMock 生成的 Mockesp_task_wdt + Mocktask(freertos) 验证 pal_wdt 对
 * TWDT 驱动的调用契约、参数校验与错误码透传。
 *
 * mock 模式：TEST_SETUP 调 Init，TEST_TEAR_DOWN 调 Verify+Destroy（esp_task_wdt）；
 * freertos 的 xTaskGetCurrentTaskHandle 用单次 Expect，不跨用例残留。
 */

#include "unity.h"
#include "unity_fixture.h"

#include "esp_err.h"
#include "Mockesp_task_wdt.h"
#include "Mocktask.h"          /* freertos mock：xTaskGetCurrentTaskHandle */

#include "pal_wdt.h"

/* ================================================================
 *  测试组 / setup / teardown
 * ================================================================ */

TEST_GROUP(pal_wdt);

TEST_SETUP(pal_wdt)
{
    Mockesp_task_wdt_Init();
}

TEST_TEAR_DOWN(pal_wdt)
{
    Mockesp_task_wdt_Verify();
    Mockesp_task_wdt_Destroy();
}

/* ================================================================
 *  pal_wdt_init / reconfigure / deinit
 * ================================================================ */

TEST(pal_wdt, init_success)
{
    pal_wdt_config_t cfg = { .timeout_ms = 5000, .idle_core_mask = 0x1, .trigger_panic = true };

    esp_task_wdt_init_ExpectAndReturn(NULL, ESP_OK);
    esp_task_wdt_init_IgnoreArg_config();

    TEST_ASSERT_EQUAL_INT(0, pal_wdt_init(&cfg));
}

TEST(pal_wdt, init_null_returns_invalid_arg)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_wdt_init(NULL));
}

TEST(pal_wdt, reconfigure_success)
{
    pal_wdt_config_t cfg = { .timeout_ms = 8000, .idle_core_mask = 0x1, .trigger_panic = false };

    esp_task_wdt_reconfigure_ExpectAndReturn(NULL, ESP_OK);
    esp_task_wdt_reconfigure_IgnoreArg_config();

    TEST_ASSERT_EQUAL_INT(0, pal_wdt_reconfigure(&cfg));
}

TEST(pal_wdt, reconfigure_null_returns_invalid_arg)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_wdt_reconfigure(NULL));
}

TEST(pal_wdt, deinit_calls_underlying)
{
    esp_task_wdt_deinit_ExpectAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_wdt_deinit());
}

/* ================================================================
 *  pal_wdt_add_user / reset_user / delete_user
 * ================================================================ */

TEST(pal_wdt, add_user_success_returns_handle)
{
    pal_wdt_user_handle_t handle = NULL;
    esp_task_wdt_user_handle_t mock_user = (esp_task_wdt_user_handle_t)0xCAFE;

    esp_task_wdt_add_user_ExpectAndReturn("test", NULL, ESP_OK);
    esp_task_wdt_add_user_IgnoreArg_user_handle_ret();
    esp_task_wdt_add_user_ReturnThruPtr_user_handle_ret(&mock_user);

    TEST_ASSERT_EQUAL_INT(0, pal_wdt_add_user("test", &handle));
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL_PTR(mock_user, handle);

    /* 清理：delete_user 应调用底层 */
    esp_task_wdt_delete_user_ExpectAndReturn(mock_user, ESP_OK);
    pal_wdt_delete_user(handle);
}

TEST(pal_wdt, add_user_null_args_returns_invalid_arg)
{
    pal_wdt_user_handle_t handle = NULL;
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_wdt_add_user(NULL, &handle));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_wdt_add_user("test", NULL));
    TEST_ASSERT_NULL(handle);
}

TEST(pal_wdt, reset_user_null_returns_invalid_arg)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_wdt_reset_user(NULL));
}

TEST(pal_wdt, delete_user_null_returns_invalid_arg)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_wdt_delete_user(NULL));
}

TEST(pal_wdt, reset_user_calls_underlying)
{
    esp_task_wdt_user_handle_t mock_user = (esp_task_wdt_user_handle_t)0xBEEF;
    pal_wdt_user_handle_t handle = NULL;

    esp_task_wdt_add_user_ExpectAndReturn("u", NULL, ESP_OK);
    esp_task_wdt_add_user_IgnoreArg_user_handle_ret();
    esp_task_wdt_add_user_ReturnThruPtr_user_handle_ret(&mock_user);
    pal_wdt_add_user("u", &handle);

    esp_task_wdt_reset_user_ExpectAndReturn(mock_user, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_wdt_reset_user(handle));

    esp_task_wdt_delete_user_ExpectAndReturn(mock_user, ESP_OK);
    pal_wdt_delete_user(handle);
}

/* ================================================================
 *  pal_wdt_add_task / pal_wdt_reset（当前任务订阅）
 * ================================================================ */

TEST(pal_wdt, add_task_calls_underlying_with_current_handle)
{
    TaskHandle_t mock_task = (TaskHandle_t)0x1234;

    /* pal_wdt_add_task 内部先取当前任务句柄，再 esp_task_wdt_add(handle) */
    xTaskGetCurrentTaskHandle_ExpectAndReturn(mock_task);
    esp_task_wdt_add_ExpectAndReturn(mock_task, ESP_OK);

    TEST_ASSERT_EQUAL_INT(0, pal_wdt_add_task());
}

TEST(pal_wdt, reset_calls_underlying)
{
    esp_task_wdt_reset_ExpectAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_wdt_reset());
}

/* ================================================================
 *  Group runner
 * ================================================================ */

TEST_GROUP_RUNNER(pal_wdt)
{
    RUN_TEST_CASE(pal_wdt, init_success);
    RUN_TEST_CASE(pal_wdt, init_null_returns_invalid_arg);
    RUN_TEST_CASE(pal_wdt, reconfigure_success);
    RUN_TEST_CASE(pal_wdt, reconfigure_null_returns_invalid_arg);
    RUN_TEST_CASE(pal_wdt, deinit_calls_underlying);
    RUN_TEST_CASE(pal_wdt, add_user_success_returns_handle);
    RUN_TEST_CASE(pal_wdt, add_user_null_args_returns_invalid_arg);
    RUN_TEST_CASE(pal_wdt, reset_user_null_returns_invalid_arg);
    RUN_TEST_CASE(pal_wdt, delete_user_null_returns_invalid_arg);
    RUN_TEST_CASE(pal_wdt, reset_user_calls_underlying);
    RUN_TEST_CASE(pal_wdt, add_task_calls_underlying_with_current_handle);
    RUN_TEST_CASE(pal_wdt, reset_calls_underlying);
}

void runner_pal_wdt(void)
{
    RUN_TEST_GROUP(pal_wdt);
}
