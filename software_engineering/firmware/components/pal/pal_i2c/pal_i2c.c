/**
 * @file    pal_i2c.c
 * @brief   PAL I2C 模块 - 实现（ESP-IDF i2c_master 新版驱动封装）
 *
 * 使用 ESP-IDF 5.x 新版 I2C 驱动 API（driver/i2c_master.h），
 * 支持同步阻塞读写。异步回调暂不暴露给 PAL 上层。
 */

#include "pal_i2c.h"

#include "driver/i2c_master.h"
#include "esp_err.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  总线管理
 * ================================================================ */

int pal_i2c_bus_init(pal_i2c_bus_handle_t *handle,
                     const pal_i2c_bus_config_t *cfg)
{
    if (handle == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port     = cfg->port,
        .sda_io_num   = cfg->sda_pin,
        .scl_io_num   = cfg->scl_pin,
        .clk_source   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority     = cfg->intr_priority,
        .trans_queue_depth = cfg->trans_queue_depth,
        .flags = {
            .enable_internal_pullup = cfg->enable_internal_pullup,
            .allow_pd               = 0,
        },
    };

    i2c_master_bus_handle_t bus_handle = NULL;
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    *handle = (pal_i2c_bus_handle_t)bus_handle;
    return ESP_OK;
}

void *pal_i2c_get_bus_handle(pal_i2c_bus_handle_t handle)
{
    return handle;  /**< PAL 内部直接存储 i2c_master_bus_handle_t */
}

int pal_i2c_bus_deinit(pal_i2c_bus_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_del_master_bus((i2c_master_bus_handle_t)handle);
}

/* ================================================================
 *  设备管理
 * ================================================================ */

int pal_i2c_dev_probe(pal_i2c_bus_handle_t bus, uint16_t addr)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 使用 i2c_master_probe 检测设备是否存在，超时 20ms */
    return i2c_master_probe((i2c_master_bus_handle_t)bus, addr, 20);
}

int pal_i2c_dev_attach(pal_i2c_dev_handle_t *dev, pal_i2c_bus_handle_t bus,
                       const pal_i2c_dev_config_t *cfg)
{
    if (dev == NULL || bus == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = cfg->device_address,
        .scl_speed_hz    = cfg->scl_speed_hz,
        .scl_wait_us     = 0, /* 使用驱动默认值 */
        .flags = {
            .disable_ack_check = cfg->disable_ack_check,
        },
    };

    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t ret = i2c_master_bus_add_device((i2c_master_bus_handle_t)bus,
                                              &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    *dev = (pal_i2c_dev_handle_t)dev_handle;
    return ESP_OK;
}

int pal_i2c_dev_detach(pal_i2c_dev_handle_t dev)
{
    if (dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_bus_rm_device((i2c_master_dev_handle_t)dev);
}

/* ================================================================
 *  数据读写
 * ================================================================ */

int pal_i2c_write(pal_i2c_dev_handle_t dev, const uint8_t *data, size_t len)
{
    if (dev == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit((i2c_master_dev_handle_t)dev,
                               data, len, 50);
}

int pal_i2c_read(pal_i2c_dev_handle_t dev, uint8_t *data, size_t len)
{
    if (dev == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_receive((i2c_master_dev_handle_t)dev,
                              data, len, 50);
}

/* ================================================================
 *  寄存器读写
 * ================================================================ */

int pal_i2c_write_reg(pal_i2c_dev_handle_t dev, uint8_t reg_addr,
                      const uint8_t *data, size_t len)
{
    if (dev == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 拼接寄存器地址 + 数据为一次传输 */
    size_t total_len = 1 + len;
    uint8_t *buf = malloc(total_len);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    buf[0] = reg_addr;
    memcpy(buf + 1, data, len);

    esp_err_t ret = i2c_master_transmit((i2c_master_dev_handle_t)dev,
                                        buf, total_len, 50);
    free(buf);
    return ret;
}

int pal_i2c_read_reg(pal_i2c_dev_handle_t dev, uint8_t reg_addr,
                     uint8_t *data, size_t len)
{
    if (dev == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 先写寄存器地址，再读数据 — 使用 i2c_master_transmit_receive */
    return i2c_master_transmit_receive((i2c_master_dev_handle_t)dev,
                                       &reg_addr, 1, data, len, 50);
}

int pal_i2c_write_reg_byte(pal_i2c_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return pal_i2c_write(dev, buf, sizeof(buf));
}

int pal_i2c_read_reg_byte(pal_i2c_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    return pal_i2c_read_reg(dev, reg, val, 1);
}
