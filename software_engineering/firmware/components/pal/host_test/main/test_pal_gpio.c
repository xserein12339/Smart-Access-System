/**
 * @file    test_pal_gpio.c
 * @brief   pal_gpio 模块 mock 单元测试
 *
 * pal_gpio 为无状态透传封装（除 ISR service 单例标记 g_isr_service_installed）。
 * 通过 Mockgpio 验证对底层 gpio 驱动的调用契约与错误码透传。
 */

#include "unity.h"
#include "unity_fixture.h"

#include "esp_err.h"
#include "Mockgpio.h"

#include "pal_gpio.h"

TEST_GROUP(pal_gpio);

TEST_SETUP(pal_gpio)
{
    Mockgpio_Init();
    /* 重置 ISR service 单例状态（pal_gpio 内部 static），避免跨用例污染 */
    gpio_uninstall_isr_service_Ignore();
    pal_gpio_uninstall_isr_service();
    gpio_uninstall_isr_service_StopIgnore();
}

TEST_TEAR_DOWN(pal_gpio)
{
    Mockgpio_Verify();
    Mockgpio_Destroy();
}

/* ================================================================
 *  引脚配置
 * ================================================================ */

TEST(pal_gpio, set_direction_calls_gpio_config)
{
    gpio_config_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_gpio_set_direction(4, PAL_GPIO_DIR_OUTPUT));
}

TEST(pal_gpio, set_direction_propagates_error)
{
    gpio_config_ExpectAnyArgsAndReturn(ESP_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_gpio_set_direction(99, PAL_GPIO_DIR_INPUT));
}

TEST(pal_gpio, set_pull_mode_calls_underlying)
{
    gpio_set_pull_mode_ExpectAndReturn(4, GPIO_PULLUP_ONLY, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_gpio_set_pull_mode(4, PAL_GPIO_PULL_UP));
}

TEST(pal_gpio, set_drive_strength_calls_underlying)
{
    gpio_set_drive_capability_ExpectAndReturn(4, (gpio_drive_cap_t)3, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_gpio_set_drive_strength(4, 3));
}

/* ================================================================
 *  输出
 * ================================================================ */

TEST(pal_gpio, write_calls_set_level)
{
    gpio_set_level_ExpectAndReturn(4, 1, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_gpio_write(4, 1));
}

TEST(pal_gpio, toggle_reads_then_writes)
{
    gpio_get_level_ExpectAndReturn(4, 1);
    gpio_set_level_ExpectAndReturn(4, 0, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_gpio_toggle(4));
}

TEST(pal_gpio, write_mask_sets_each_pin)
{
    uint64_t mask = (uint64_t)1 << 4 | (uint64_t)1 << 5;
    gpio_set_level_ExpectAndReturn(4, 1, ESP_OK);
    gpio_set_level_ExpectAndReturn(5, 0, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_gpio_write_mask(mask, (uint64_t)1 << 4));
}

/* ================================================================
 *  输入
 * ================================================================ */

TEST(pal_gpio, read_calls_get_level)
{
    gpio_get_level_ExpectAndReturn(4, 1);
    TEST_ASSERT_EQUAL_INT(1, pal_gpio_read(4));
}

TEST(pal_gpio, read_mask_aggregates_levels)
{
    gpio_get_level_ExpectAndReturn(4, 1);
    gpio_get_level_ExpectAndReturn(5, 0);
    int64_t r = pal_gpio_read_mask((uint64_t)1 << 4 | (uint64_t)1 << 5);
    TEST_ASSERT_EQUAL_INT64((int64_t)1 << 4, r);
}

/* ================================================================
 *  中断
 * ================================================================ */

TEST(pal_gpio, set_intr_posedge_registers_handler)
{
    gpio_set_intr_type_ExpectAndReturn(4, GPIO_INTR_POSEDGE, ESP_OK);
    gpio_isr_handler_add_ExpectAndReturn(4, NULL, NULL, ESP_OK);
    gpio_isr_handler_add_IgnoreArg_isr_handler();
    gpio_isr_handler_add_IgnoreArg_args();

    pal_gpio_isr_cb_t cb = (pal_gpio_isr_cb_t)0x1234;
    TEST_ASSERT_EQUAL_INT(0, pal_gpio_set_intr(4, PAL_GPIO_INTR_POSEDGE, cb, NULL));
}

TEST(pal_gpio, set_intr_disable_skips_handler)
{
    /* edge=DISABLE 时只调 set_intr_type，不注册 handler */
    gpio_set_intr_type_ExpectAndReturn(4, GPIO_INTR_DISABLE, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_gpio_set_intr(4, PAL_GPIO_INTR_DISABLE, NULL, NULL));
}

TEST(pal_gpio, set_intr_propagates_set_intr_type_error)
{
    gpio_set_intr_type_ExpectAndReturn(4, GPIO_INTR_NEGEDGE, ESP_FAIL);
    TEST_ASSERT_EQUAL_INT(ESP_FAIL, pal_gpio_set_intr(4, PAL_GPIO_INTR_NEGEDGE,
                                                      (pal_gpio_isr_cb_t)0x1, NULL));
}

TEST(pal_gpio, intr_enable_disable_passthrough)
{
    gpio_intr_enable_ExpectAndReturn(4, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_gpio_intr_enable(4));

    gpio_intr_disable_ExpectAndReturn(4, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_gpio_intr_disable(4));
}

TEST(pal_gpio, remove_intr_disables_then_removes_handler)
{
    gpio_intr_disable_ExpectAndReturn(4, ESP_OK);
    gpio_isr_handler_remove_ExpectAndReturn(4, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_gpio_remove_intr(4));
}

TEST(pal_gpio, install_uninstall_isr_service)
{
    gpio_install_isr_service_ExpectAndReturn(ESP_INTR_FLAG_IRAM, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_gpio_install_isr_service(ESP_INTR_FLAG_IRAM));

    /* 二次安装应直接返回 OK，不再触底层 */
    TEST_ASSERT_EQUAL_INT(0, pal_gpio_install_isr_service(ESP_INTR_FLAG_IRAM));

    gpio_uninstall_isr_service_Expect();
    pal_gpio_uninstall_isr_service();
}

TEST_GROUP_RUNNER(pal_gpio)
{
    RUN_TEST_CASE(pal_gpio, set_direction_calls_gpio_config);
    RUN_TEST_CASE(pal_gpio, set_direction_propagates_error);
    RUN_TEST_CASE(pal_gpio, set_pull_mode_calls_underlying);
    RUN_TEST_CASE(pal_gpio, set_drive_strength_calls_underlying);
    RUN_TEST_CASE(pal_gpio, write_calls_set_level);
    RUN_TEST_CASE(pal_gpio, toggle_reads_then_writes);
    RUN_TEST_CASE(pal_gpio, write_mask_sets_each_pin);
    RUN_TEST_CASE(pal_gpio, read_calls_get_level);
    RUN_TEST_CASE(pal_gpio, read_mask_aggregates_levels);
    RUN_TEST_CASE(pal_gpio, set_intr_posedge_registers_handler);
    RUN_TEST_CASE(pal_gpio, set_intr_disable_skips_handler);
    RUN_TEST_CASE(pal_gpio, set_intr_propagates_set_intr_type_error);
    RUN_TEST_CASE(pal_gpio, intr_enable_disable_passthrough);
    RUN_TEST_CASE(pal_gpio, remove_intr_disables_then_removes_handler);
    RUN_TEST_CASE(pal_gpio, install_uninstall_isr_service);
}

void runner_pal_gpio(void)
{
    RUN_TEST_GROUP(pal_gpio);
}
