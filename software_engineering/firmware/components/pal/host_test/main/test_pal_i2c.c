/**
 * @file    test_pal_i2c.c
 * @brief   pal_i2c 模块 mock 单元测试
 *
 * pal_i2c 句柄直接存储底层 i2c_master_bus_handle_t / i2c_master_dev_handle_t
 * （无内部结构体），通过 Mocki2c_master 验证调用契约、参数校验与错误码透传。
 */

#include "unity.h"
#include "unity_fixture.h"

#include "esp_err.h"
#include "Mocki2c_master.h"

#include "pal_i2c.h"

TEST_GROUP(pal_i2c);

TEST_SETUP(pal_i2c)
{
    Mocki2c_master_Init();
}

TEST_TEAR_DOWN(pal_i2c)
{
    Mocki2c_master_Verify();
    Mocki2c_master_Destroy();
}

/* ================================================================
 *  总线
 * ================================================================ */

TEST(pal_i2c, bus_init_success)
{
    pal_i2c_bus_handle_t handle = NULL;
    pal_i2c_bus_config_t cfg = {
        .port = 0, .sda_pin = 8, .scl_pin = 9, .freq_hz = 100000,
        .enable_internal_pullup = true, .intr_priority = 0, .trans_queue_depth = 10,
    };
    i2c_master_bus_handle_t mock_bus = (i2c_master_bus_handle_t)0xA000;

    i2c_new_master_bus_ExpectAnyArgsAndReturn(ESP_OK);
    i2c_new_master_bus_ReturnThruPtr_ret_bus_handle(&mock_bus);

    TEST_ASSERT_EQUAL_INT(0, pal_i2c_bus_init(&handle, &cfg));
    TEST_ASSERT_EQUAL_PTR(mock_bus, handle);
}

TEST(pal_i2c, bus_init_null_args)
{
    pal_i2c_bus_handle_t h = NULL;
    pal_i2c_bus_config_t cfg = {0};
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_bus_init(NULL, &cfg));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_bus_init(&h, NULL));
}

TEST(pal_i2c, bus_init_driver_failure)
{
    pal_i2c_bus_handle_t handle = NULL;
    pal_i2c_bus_config_t cfg = { .sda_pin = 8, .scl_pin = 9 };

    i2c_new_master_bus_ExpectAnyArgsAndReturn(ESP_ERR_INVALID_STATE);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, pal_i2c_bus_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

TEST(pal_i2c, bus_deinit_calls_underlying)
{
    pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)0xA000;
    i2c_del_master_bus_ExpectAndReturn((i2c_master_bus_handle_t)bus, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_i2c_bus_deinit(bus));
}

TEST(pal_i2c, bus_deinit_null)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_bus_deinit(NULL));
}

TEST(pal_i2c, get_bus_handle_returns_handle)
{
    pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)0xA000;
    TEST_ASSERT_EQUAL_PTR(bus, pal_i2c_get_bus_handle(bus));
}

/* ================================================================
 *  设备
 * ================================================================ */

TEST(pal_i2c, dev_probe_calls_underlying)
{
    pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)0xA000;
    i2c_master_probe_ExpectAndReturn((i2c_master_bus_handle_t)bus, 0x38, 20, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_i2c_dev_probe(bus, 0x38));
}

TEST(pal_i2c, dev_probe_null_bus)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_dev_probe(NULL, 0x38));
}

TEST(pal_i2c, dev_attach_success)
{
    pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)0xA000;
    pal_i2c_dev_handle_t dev = NULL;
    pal_i2c_dev_config_t cfg = { .device_address = 0x38, .scl_speed_hz = 100000 };
    i2c_master_dev_handle_t mock_dev = (i2c_master_dev_handle_t)0xB000;

    i2c_master_bus_add_device_ExpectAnyArgsAndReturn(ESP_OK);
    i2c_master_bus_add_device_ReturnThruPtr_ret_handle(&mock_dev);

    TEST_ASSERT_EQUAL_INT(0, pal_i2c_dev_attach(&dev, bus, &cfg));
    TEST_ASSERT_EQUAL_PTR(mock_dev, dev);
}

TEST(pal_i2c, dev_attach_null_args)
{
    pal_i2c_dev_handle_t d = NULL;
    pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)0xA000;
    pal_i2c_dev_config_t cfg = {0};
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_dev_attach(NULL, bus, &cfg));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_dev_attach(&d, NULL, &cfg));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_dev_attach(&d, bus, NULL));
}

TEST(pal_i2c, dev_detach_calls_underlying)
{
    pal_i2c_dev_handle_t dev = (pal_i2c_dev_handle_t)0xB000;
    i2c_master_bus_rm_device_ExpectAndReturn((i2c_master_dev_handle_t)dev, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_i2c_dev_detach(dev));
}

TEST(pal_i2c, dev_detach_null)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_dev_detach(NULL));
}

/* ================================================================
 *  数据读写
 * ================================================================ */

TEST(pal_i2c, write_calls_transmit)
{
    pal_i2c_dev_handle_t dev = (pal_i2c_dev_handle_t)0xB000;
    uint8_t data[2] = {0x01, 0x02};
    i2c_master_transmit_ExpectAndReturn((i2c_master_dev_handle_t)dev, data, 2, 50, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_i2c_write(dev, data, 2));
}

TEST(pal_i2c, write_null_args)
{
    pal_i2c_dev_handle_t dev = (pal_i2c_dev_handle_t)0xB000;
    uint8_t data[2] = {0};
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_write(NULL, data, 2));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_write(dev, NULL, 2));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_write(dev, data, 0));
}

TEST(pal_i2c, read_calls_receive)
{
    pal_i2c_dev_handle_t dev = (pal_i2c_dev_handle_t)0xB000;
    uint8_t buf[2];
    i2c_master_receive_ExpectAndReturn((i2c_master_dev_handle_t)dev, buf, 2, 50, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_i2c_read(dev, buf, 2));
}

TEST(pal_i2c, read_null_args)
{
    pal_i2c_dev_handle_t dev = (pal_i2c_dev_handle_t)0xB000;
    uint8_t buf[2];
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_read(NULL, buf, 2));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_read(dev, NULL, 2));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_read(dev, buf, 0));
}

TEST(pal_i2c, write_reg_calls_transmit_with_addr_prefix)
{
    pal_i2c_dev_handle_t dev = (pal_i2c_dev_handle_t)0xB000;
    uint8_t payload[2] = {0xAA, 0xBB};
    /* write_reg 内部 malloc 拼接 reg+payload，buffer 地址不确定，用 ExpectAnyArgs */
    i2c_master_transmit_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_i2c_write_reg(dev, 0x10, payload, 2));
}

TEST(pal_i2c, write_reg_null_args)
{
    pal_i2c_dev_handle_t dev = (pal_i2c_dev_handle_t)0xB000;
    uint8_t data[1] = {0};
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_write_reg(NULL, 0x10, data, 1));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_write_reg(dev, 0x10, NULL, 1));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_write_reg(dev, 0x10, data, 0));
}

TEST(pal_i2c, read_reg_calls_transmit_receive)
{
    pal_i2c_dev_handle_t dev = (pal_i2c_dev_handle_t)0xB000;
    uint8_t buf[2];
    i2c_master_transmit_receive_ExpectAndReturn((i2c_master_dev_handle_t)dev,
                                                NULL, 1, buf, 2, 50, ESP_OK);
    i2c_master_transmit_receive_IgnoreArg_write_buffer();
    TEST_ASSERT_EQUAL_INT(0, pal_i2c_read_reg(dev, 0x10, buf, 2));
}

TEST(pal_i2c, read_reg_null_args)
{
    pal_i2c_dev_handle_t dev = (pal_i2c_dev_handle_t)0xB000;
    uint8_t buf[2];
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_read_reg(NULL, 0x10, buf, 2));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_read_reg(dev, 0x10, NULL, 2));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_i2c_read_reg(dev, 0x10, buf, 0));
}

TEST(pal_i2c, write_reg_byte_calls_write)
{
    pal_i2c_dev_handle_t dev = (pal_i2c_dev_handle_t)0xB000;
    /* write_reg_byte 内部构造 2 字节 buf 调 pal_i2c_write → i2c_master_transmit */
    i2c_master_transmit_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_i2c_write_reg_byte(dev, 0x10, 0xFF));
}

TEST_GROUP_RUNNER(pal_i2c)
{
    RUN_TEST_CASE(pal_i2c, bus_init_success);
    RUN_TEST_CASE(pal_i2c, bus_init_null_args);
    RUN_TEST_CASE(pal_i2c, bus_init_driver_failure);
    RUN_TEST_CASE(pal_i2c, bus_deinit_calls_underlying);
    RUN_TEST_CASE(pal_i2c, bus_deinit_null);
    RUN_TEST_CASE(pal_i2c, get_bus_handle_returns_handle);
    RUN_TEST_CASE(pal_i2c, dev_probe_calls_underlying);
    RUN_TEST_CASE(pal_i2c, dev_probe_null_bus);
    RUN_TEST_CASE(pal_i2c, dev_attach_success);
    RUN_TEST_CASE(pal_i2c, dev_attach_null_args);
    RUN_TEST_CASE(pal_i2c, dev_detach_calls_underlying);
    RUN_TEST_CASE(pal_i2c, dev_detach_null);
    RUN_TEST_CASE(pal_i2c, write_calls_transmit);
    RUN_TEST_CASE(pal_i2c, write_null_args);
    RUN_TEST_CASE(pal_i2c, read_calls_receive);
    RUN_TEST_CASE(pal_i2c, read_null_args);
    RUN_TEST_CASE(pal_i2c, write_reg_calls_transmit_with_addr_prefix);
    RUN_TEST_CASE(pal_i2c, write_reg_null_args);
    RUN_TEST_CASE(pal_i2c, read_reg_calls_transmit_receive);
    RUN_TEST_CASE(pal_i2c, read_reg_null_args);
    RUN_TEST_CASE(pal_i2c, write_reg_byte_calls_write);
}

void runner_pal_i2c(void)
{
    RUN_TEST_GROUP(pal_i2c);
}
