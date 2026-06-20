/**
 * @file    test_pal_uart.c
 * @brief   pal_uart 模块 mock 单元测试
 *
 * pal_uart 维护内部结构体 pal_uart_internal_t {port, inited}。
 * init 失败路径在 driver_install 之前不调 uart_driver_delete。
 * 通过 Mockuart 验证生命周期、调用契约与错误码透传。
 */

#include "unity.h"
#include "unity_fixture.h"

#include "esp_err.h"
#include "Mockuart.h"

#include "pal_uart.h"

TEST_GROUP(pal_uart);

TEST_SETUP(pal_uart)
{
    Mockuart_Init();
}

TEST_TEAR_DOWN(pal_uart)
{
    Mockuart_Verify();
    Mockuart_Destroy();
}

/* ================================================================
 *  辅助
 * ================================================================ */

static pal_uart_config_t default_cfg(void)
{
    pal_uart_config_t cfg = {
        .port_num = 1, .tx_pin = 4, .rx_pin = 5, .rts_pin = -1, .cts_pin = -1,
        .baud_rate = 115200, .data_bits = PAL_UART_DATA_8,
        .stop_bits = PAL_UART_STOP_1, .parity = PAL_UART_PARITY_NONE,
        .flow_ctrl = PAL_UART_HW_FLOWCTRL_DISABLE,
        .rx_buf_size = 256, .tx_buf_size = 0,
    };
    return cfg;
}

/* 模拟 init 全程成功的三个底层调用 */
static void expect_init_success(int port)
{
    uart_param_config_ExpectAnyArgsAndReturn(ESP_OK);
    uart_set_pin_ExpectAndReturn(port, 4, 5, -1, -1, ESP_OK);
    uart_driver_install_ExpectAndReturn(port, 256, 0, 0, NULL, 0, ESP_OK);
}

/* ================================================================
 *  生命周期
 * ================================================================ */

TEST(pal_uart, init_success)
{
    pal_uart_handle_t handle = NULL;
    pal_uart_config_t cfg = default_cfg();
    expect_init_success(cfg.port_num);

    TEST_ASSERT_EQUAL_INT(0, pal_uart_init(&handle, &cfg));
    TEST_ASSERT_NOT_NULL(handle);

    uart_driver_delete_ExpectAndReturn(cfg.port_num, ESP_OK);
    pal_uart_deinit(handle);
}

TEST(pal_uart, init_null_args)
{
    pal_uart_handle_t h = NULL;
    pal_uart_config_t cfg = default_cfg();
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_uart_init(NULL, &cfg));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_uart_init(&h, NULL));
}

TEST(pal_uart, init_param_config_failure_frees_struct)
{
    pal_uart_handle_t handle = NULL;
    pal_uart_config_t cfg = default_cfg();
    uart_param_config_ExpectAnyArgsAndReturn(ESP_ERR_INVALID_ARG);
    /* param_config 失败 → 直接 free，不调 set_pin/install/delete */
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_uart_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

TEST(pal_uart, init_set_pin_failure_frees_struct)
{
    pal_uart_handle_t handle = NULL;
    pal_uart_config_t cfg = default_cfg();
    uart_param_config_ExpectAnyArgsAndReturn(ESP_OK);
    uart_set_pin_ExpectAndReturn(cfg.port_num, 4, 5, -1, -1, ESP_FAIL);
    /* set_pin 失败 → free，不调 install/delete */
    TEST_ASSERT_EQUAL_INT(ESP_FAIL, pal_uart_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

TEST(pal_uart, init_driver_install_failure_frees_struct)
{
    pal_uart_handle_t handle = NULL;
    pal_uart_config_t cfg = default_cfg();
    uart_param_config_ExpectAnyArgsAndReturn(ESP_OK);
    uart_set_pin_ExpectAndReturn(cfg.port_num, 4, 5, -1, -1, ESP_OK);
    uart_driver_install_ExpectAndReturn(cfg.port_num, 256, 0, 0, NULL, 0, ESP_ERR_NO_MEM);
    /* install 失败 → free，不调 delete */
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NO_MEM, pal_uart_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

TEST(pal_uart, deinit_calls_driver_delete)
{
    pal_uart_handle_t handle = NULL;
    pal_uart_config_t cfg = default_cfg();
    expect_init_success(cfg.port_num);
    pal_uart_init(&handle, &cfg);

    uart_driver_delete_ExpectAndReturn(cfg.port_num, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_uart_deinit(handle));
}

TEST(pal_uart, deinit_null)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_uart_deinit(NULL));
}

/* ================================================================
 *  运行时
 * ================================================================ */

TEST(pal_uart, set_baudrate_calls_underlying)
{
    pal_uart_handle_t handle = NULL;
    pal_uart_config_t cfg = default_cfg();
    expect_init_success(cfg.port_num);
    pal_uart_init(&handle, &cfg);

    uart_set_baudrate_ExpectAndReturn(cfg.port_num, 921600, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_uart_set_baudrate(handle, 921600));

    uart_driver_delete_ExpectAndReturn(cfg.port_num, ESP_OK);
    pal_uart_deinit(handle);
}

TEST(pal_uart, set_baudrate_null)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_uart_set_baudrate(NULL, 921600));
}

TEST(pal_uart, send_returns_bytes_written)
{
    pal_uart_handle_t handle = NULL;
    pal_uart_config_t cfg = default_cfg();
    expect_init_success(cfg.port_num);
    pal_uart_init(&handle, &cfg);

    uint8_t data[4] = {1, 2, 3, 4};
    uart_write_bytes_ExpectAndReturn(cfg.port_num, data, 4, 4);
    TEST_ASSERT_EQUAL_INT(4, pal_uart_send(handle, data, 4));

    uart_driver_delete_ExpectAndReturn(cfg.port_num, ESP_OK);
    pal_uart_deinit(handle);
}

TEST(pal_uart, send_null_args)
{
    pal_uart_handle_t handle = NULL;
    pal_uart_config_t cfg = default_cfg();
    expect_init_success(cfg.port_num);
    pal_uart_init(&handle, &cfg);

    uint8_t data[1] = {0};
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_uart_send(NULL, data, 1));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_uart_send(handle, NULL, 1));

    uart_driver_delete_ExpectAndReturn(cfg.port_num, ESP_OK);
    pal_uart_deinit(handle);
}

TEST(pal_uart, recv_calls_read_bytes)
{
    pal_uart_handle_t handle = NULL;
    pal_uart_config_t cfg = default_cfg();
    expect_init_success(cfg.port_num);
    pal_uart_init(&handle, &cfg);

    uint8_t buf[4];
    /* recv 内部用 pdMS_TO_TICKS(timeout_ms)，ticks 值依赖配置，用 ExpectAnyArgs */
    uart_read_bytes_ExpectAnyArgsAndReturn(2);
    TEST_ASSERT_EQUAL_INT(2, pal_uart_recv(handle, buf, 4, 100));

    uart_driver_delete_ExpectAndReturn(cfg.port_num, ESP_OK);
    pal_uart_deinit(handle);
}

TEST(pal_uart, recv_null_args)
{
    pal_uart_handle_t handle = NULL;
    pal_uart_config_t cfg = default_cfg();
    expect_init_success(cfg.port_num);
    pal_uart_init(&handle, &cfg);

    uint8_t buf[4];
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_uart_recv(NULL, buf, 4, 100));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_uart_recv(handle, NULL, 4, 100));

    uart_driver_delete_ExpectAndReturn(cfg.port_num, ESP_OK);
    pal_uart_deinit(handle);
}

TEST(pal_uart, get_rx_buffered_len_returns_len)
{
    pal_uart_handle_t handle = NULL;
    pal_uart_config_t cfg = default_cfg();
    expect_init_success(cfg.port_num);
    pal_uart_init(&handle, &cfg);

    size_t len = 42;
    uart_get_buffered_data_len_ExpectAndReturn(cfg.port_num, NULL, ESP_OK);
    uart_get_buffered_data_len_IgnoreArg_size();
    uart_get_buffered_data_len_ReturnThruPtr_size(&len);
    TEST_ASSERT_EQUAL_INT(42, pal_uart_get_rx_buffered_len(handle));

    uart_driver_delete_ExpectAndReturn(cfg.port_num, ESP_OK);
    pal_uart_deinit(handle);
}

TEST(pal_uart, get_rx_buffered_len_null)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_uart_get_rx_buffered_len(NULL));
}

TEST_GROUP_RUNNER(pal_uart)
{
    RUN_TEST_CASE(pal_uart, init_success);
    RUN_TEST_CASE(pal_uart, init_null_args);
    RUN_TEST_CASE(pal_uart, init_param_config_failure_frees_struct);
    RUN_TEST_CASE(pal_uart, init_set_pin_failure_frees_struct);
    RUN_TEST_CASE(pal_uart, init_driver_install_failure_frees_struct);
    RUN_TEST_CASE(pal_uart, deinit_calls_driver_delete);
    RUN_TEST_CASE(pal_uart, deinit_null);
    RUN_TEST_CASE(pal_uart, set_baudrate_calls_underlying);
    RUN_TEST_CASE(pal_uart, set_baudrate_null);
    RUN_TEST_CASE(pal_uart, send_returns_bytes_written);
    RUN_TEST_CASE(pal_uart, send_null_args);
    RUN_TEST_CASE(pal_uart, recv_calls_read_bytes);
    RUN_TEST_CASE(pal_uart, recv_null_args);
    RUN_TEST_CASE(pal_uart, get_rx_buffered_len_returns_len);
    RUN_TEST_CASE(pal_uart, get_rx_buffered_len_null);
}

void runner_pal_uart(void)
{
    RUN_TEST_GROUP(pal_uart);
}
