/**
 * @file    test_pal_spi.c
 * @brief   pal_spi 模块 mock 单元测试
 *
 * pal_spi 维护内部结构体 pal_spi_bus_internal_t / pal_spi_dev_internal_t。
 * 通过 Mockspi_master + Mockspi_common 验证生命周期、调用契约与错误码透传。
 */

#include "unity.h"
#include "unity_fixture.h"

#include "esp_err.h"
#include "Mockspi_master.h"
#include "Mockspi_common.h"

#include "pal_spi.h"

TEST_GROUP(pal_spi);

TEST_SETUP(pal_spi)
{
    Mockspi_master_Init();
    Mockspi_common_Init();
}

TEST_TEAR_DOWN(pal_spi)
{
    Mockspi_master_Verify();
    Mockspi_master_Destroy();
    Mockspi_common_Verify();
    Mockspi_common_Destroy();
}

/* ================================================================
 *  辅助：acquire 一个可用 bus 句柄
 * ================================================================ */
static pal_spi_bus_handle_t acquire_bus(spi_host_device_t host)
{
    pal_spi_bus_handle_t handle = NULL;
    pal_spi_bus_config_t cfg = {
        .host = (int)host, .mosi_pin = 11, .miso_pin = 12, .sclk_pin = 10,
        .quadwp_pin = -1, .quadhd_pin = -1, .max_transfer_sz = 4096,
    };
    spi_bus_initialize_ExpectAnyArgsAndReturn(ESP_OK);
    pal_spi_bus_init(&handle, &cfg);
    return handle;
}

/* ================================================================
 *  总线
 * ================================================================ */

TEST(pal_spi, bus_init_success)
{
    pal_spi_bus_handle_t handle = NULL;
    pal_spi_bus_config_t cfg = {
        .host = (int)SPI2_HOST, .mosi_pin = 11, .miso_pin = 12, .sclk_pin = 10,
        .quadwp_pin = -1, .quadhd_pin = -1, .max_transfer_sz = 4096,
    };
    spi_bus_initialize_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_spi_bus_init(&handle, &cfg));
    TEST_ASSERT_NOT_NULL(handle);

    /* 清理：deinit 内部 spi_bus_free(bus->host)，bus->host = cfg.host = SPI2_HOST */
    spi_bus_free_ExpectAndReturn(SPI2_HOST, ESP_OK);
    pal_spi_bus_deinit(handle);
}

TEST(pal_spi, bus_init_null_args)
{
    pal_spi_bus_handle_t h = NULL;
    pal_spi_bus_config_t cfg = {0};
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_spi_bus_init(NULL, &cfg));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_spi_bus_init(&h, NULL));
}

TEST(pal_spi, bus_init_driver_failure_frees_struct)
{
    pal_spi_bus_handle_t handle = NULL;
    pal_spi_bus_config_t cfg = { .host = 2, .sclk_pin = 10 };
    spi_bus_initialize_ExpectAnyArgsAndReturn(ESP_ERR_INVALID_STATE);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, pal_spi_bus_init(&handle, &cfg));
    TEST_ASSERT_NULL(handle);
}

TEST(pal_spi, bus_deinit_calls_free)
{
    pal_spi_bus_handle_t bus = acquire_bus(SPI2_HOST);
    spi_bus_free_ExpectAndReturn(SPI2_HOST, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_spi_bus_deinit(bus));
}

TEST(pal_spi, bus_deinit_null)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_spi_bus_deinit(NULL));
}

/* ================================================================
 *  设备
 * ================================================================ */

TEST(pal_spi, dev_attach_success)
{
    pal_spi_bus_handle_t bus = acquire_bus(SPI2_HOST);
    pal_spi_dev_handle_t dev = NULL;
    pal_spi_dev_config_t cfg = {
        .cs_pin = 9, .freq_hz = 1000000, .mode = PAL_SPI_MODE_0,
        .queue_size = 4,
    };
    spi_device_handle_t mock_dev = (spi_device_handle_t)0xD000;

    spi_bus_add_device_ExpectAnyArgsAndReturn(ESP_OK);
    spi_bus_add_device_ReturnThruPtr_handle(&mock_dev);

    TEST_ASSERT_EQUAL_INT(0, pal_spi_dev_attach(&dev, bus, &cfg));
    TEST_ASSERT_NOT_NULL(dev);

    /* 清理 */
    spi_bus_remove_device_ExpectAndReturn(mock_dev, ESP_OK);
    pal_spi_dev_detach(dev);
    spi_bus_free_ExpectAndReturn(SPI2_HOST, ESP_OK);
    pal_spi_bus_deinit(bus);
}

TEST(pal_spi, dev_attach_null_args)
{
    pal_spi_bus_handle_t bus = acquire_bus(SPI2_HOST);
    pal_spi_dev_handle_t d = NULL;
    pal_spi_dev_config_t cfg = {0};
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_spi_dev_attach(NULL, bus, &cfg));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_spi_dev_attach(&d, NULL, &cfg));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_spi_dev_attach(&d, bus, NULL));

    spi_bus_free_ExpectAndReturn(SPI2_HOST, ESP_OK);
    pal_spi_bus_deinit(bus);
}

TEST(pal_spi, dev_detach_calls_remove)
{
    pal_spi_bus_handle_t bus = acquire_bus(SPI2_HOST);
    pal_spi_dev_handle_t dev = NULL;
    pal_spi_dev_config_t cfg = { .cs_pin = 9, .freq_hz = 1000000, .mode = PAL_SPI_MODE_0 };
    spi_device_handle_t mock_dev = (spi_device_handle_t)0xD000;

    spi_bus_add_device_ExpectAnyArgsAndReturn(ESP_OK);
    spi_bus_add_device_ReturnThruPtr_handle(&mock_dev);
    pal_spi_dev_attach(&dev, bus, &cfg);

    spi_bus_remove_device_ExpectAndReturn(mock_dev, ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_spi_dev_detach(dev));

    spi_bus_free_ExpectAndReturn(SPI2_HOST, ESP_OK);
    pal_spi_bus_deinit(bus);
}

/* ================================================================
 *  传输
 * ================================================================ */

static pal_spi_dev_handle_t acquire_dev(spi_host_device_t host, spi_device_handle_t mock_dev)
{
    pal_spi_bus_handle_t bus = acquire_bus(host);
    pal_spi_dev_handle_t dev = NULL;
    pal_spi_dev_config_t cfg = { .cs_pin = 9, .freq_hz = 1000000, .mode = PAL_SPI_MODE_0 };
    spi_bus_add_device_ExpectAnyArgsAndReturn(ESP_OK);
    spi_bus_add_device_ReturnThruPtr_handle(&mock_dev);
    pal_spi_dev_attach(&dev, bus, &cfg);
    return dev;
}

TEST(pal_spi, transfer_calls_device_transmit)
{
    spi_device_handle_t mock_dev = (spi_device_handle_t)0xD000;
    pal_spi_dev_handle_t dev = acquire_dev(SPI2_HOST, mock_dev);
    uint8_t tx[2] = {0xAA, 0xBB};
    uint8_t rx[2];

    spi_device_transmit_ExpectAndReturn(mock_dev, NULL, ESP_OK);
    spi_device_transmit_IgnoreArg_trans_desc();
    TEST_ASSERT_EQUAL_INT(0, pal_spi_transfer(dev, tx, rx, 2));
}

TEST(pal_spi, transfer_null_dev)
{
    uint8_t tx[2] = {0};
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_spi_transfer(NULL, tx, NULL, 2));
}

TEST(pal_spi, transmit_ignores_rx)
{
    spi_device_handle_t mock_dev = (spi_device_handle_t)0xD000;
    pal_spi_dev_handle_t dev = acquire_dev(SPI2_HOST, mock_dev);
    uint8_t data[2] = {0x01, 0x02};

    /* transmit 内部调 pal_spi_transfer(dev, data, NULL, len) */
    spi_device_transmit_ExpectAndReturn(mock_dev, NULL, ESP_OK);
    spi_device_transmit_IgnoreArg_trans_desc();
    TEST_ASSERT_EQUAL_INT(0, pal_spi_transmit(dev, data, 2));
}

TEST(pal_spi, transmit_receive_allocs_buffers)
{
    spi_device_handle_t mock_dev = (spi_device_handle_t)0xD000;
    pal_spi_dev_handle_t dev = acquire_dev(SPI2_HOST, mock_dev);
    uint8_t cmd[1] = {0x9F};
    uint8_t rx[3];

    /* transmit_receive 内部 calloc tx/rx 缓冲，trans 为局部，用 ExpectAnyArgs */
    spi_device_transmit_ExpectAnyArgsAndReturn(ESP_OK);
    TEST_ASSERT_EQUAL_INT(0, pal_spi_transmit_receive(dev, cmd, 1, rx, 3));
}

TEST(pal_spi, transmit_receive_null_args)
{
    spi_device_handle_t mock_dev = (spi_device_handle_t)0xD000;
    pal_spi_dev_handle_t dev = acquire_dev(SPI2_HOST, mock_dev);
    uint8_t cmd[1] = {0};
    uint8_t rx[3];
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_spi_transmit_receive(NULL, cmd, 1, rx, 3));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_spi_transmit_receive(dev, NULL, 1, rx, 3));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, pal_spi_transmit_receive(dev, cmd, 1, NULL, 3));
}

TEST_GROUP_RUNNER(pal_spi)
{
    RUN_TEST_CASE(pal_spi, bus_init_success);
    RUN_TEST_CASE(pal_spi, bus_init_null_args);
    RUN_TEST_CASE(pal_spi, bus_init_driver_failure_frees_struct);
    RUN_TEST_CASE(pal_spi, bus_deinit_calls_free);
    RUN_TEST_CASE(pal_spi, bus_deinit_null);
    RUN_TEST_CASE(pal_spi, dev_attach_success);
    RUN_TEST_CASE(pal_spi, dev_attach_null_args);
    RUN_TEST_CASE(pal_spi, dev_detach_calls_remove);
    RUN_TEST_CASE(pal_spi, transfer_calls_device_transmit);
    RUN_TEST_CASE(pal_spi, transfer_null_dev);
    RUN_TEST_CASE(pal_spi, transmit_ignores_rx);
    RUN_TEST_CASE(pal_spi, transmit_receive_allocs_buffers);
    RUN_TEST_CASE(pal_spi, transmit_receive_null_args);
}

void runner_pal_spi(void)
{
    RUN_TEST_GROUP(pal_spi);
}
