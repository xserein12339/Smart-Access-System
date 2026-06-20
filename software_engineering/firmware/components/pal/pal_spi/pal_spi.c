/**
 * @file    pal_spi.c
 * @brief   PAL SPI 模块 - 实现（ESP-IDF spi_master 驱动封装）
 */

#include "pal_spi.h"

#include "driver/spi_master.h"
#include "driver/spi_common.h"
#include "esp_err.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  内部结构体
 * ================================================================ */

/**
 * @brief SPI 总线内部结构（持有 spi_host_device_t）
 */
typedef struct {
    spi_host_device_t host;
    bool              inited;
} pal_spi_bus_internal_t;

/**
 * @brief SPI 设备内部结构（持有 spi_device_handle_t）
 */
typedef struct {
    spi_device_handle_t dev_handle;
    pal_spi_bus_handle_t bus;
} pal_spi_dev_internal_t;

/* ================================================================
 *  总线管理
 * ================================================================ */

int pal_spi_bus_init(pal_spi_bus_handle_t *handle,
                     const pal_spi_bus_config_t *cfg)
{
    if (handle == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pal_spi_bus_internal_t *bus = calloc(1, sizeof(*bus));
    if (bus == NULL) {
        return ESP_ERR_NO_MEM;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = cfg->mosi_pin,
        .miso_io_num     = cfg->miso_pin,
        .sclk_io_num     = cfg->sclk_pin,
        .quadwp_io_num   = cfg->quadwp_pin,
        .quadhd_io_num   = cfg->quadhd_pin,
        .data4_io_num    = -1,
        .data5_io_num    = -1,
        .data6_io_num    = -1,
        .data7_io_num    = -1,
        .max_transfer_sz = cfg->max_transfer_sz > 0 ? cfg->max_transfer_sz : 4096,
        .flags           = cfg->flags,
        .isr_cpu_id      = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags      = cfg->intr_flags,
    };

    esp_err_t ret = spi_bus_initialize((spi_host_device_t)cfg->host,
                                       &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        free(bus);
        return ret;
    }

    bus->host   = (spi_host_device_t)cfg->host;
    bus->inited = true;
    *handle = (pal_spi_bus_handle_t)bus;
    return ESP_OK;
}

int pal_spi_bus_deinit(pal_spi_bus_handle_t handle)
{
    pal_spi_bus_internal_t *bus = (pal_spi_bus_internal_t *)handle;
    if (bus == NULL || !bus->inited) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = spi_bus_free(bus->host);
    free(bus);
    return ret;
}

/* ================================================================
 *  设备管理
 * ================================================================ */

int pal_spi_dev_attach(pal_spi_dev_handle_t *dev, pal_spi_bus_handle_t bus,
                       const pal_spi_dev_config_t *cfg)
{
    if (dev == NULL || bus == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pal_spi_bus_internal_t *bus_int = (pal_spi_bus_internal_t *)bus;
    if (!bus_int->inited) {
        return ESP_ERR_INVALID_STATE;
    }

    pal_spi_dev_internal_t *dev_int = calloc(1, sizeof(*dev_int));
    if (dev_int == NULL) {
        return ESP_ERR_NO_MEM;
    }

    spi_device_interface_config_t dev_cfg = {
        .command_bits     = 0,
        .address_bits     = 0,
        .dummy_bits       = 0,
        .mode             = (uint8_t)cfg->mode,
        .duty_cycle_pos   = 0,
        .cs_ena_pretrans  = cfg->cs_ena_pretrans,
        .cs_ena_posttrans = cfg->cs_ena_posttrans,
        .clock_speed_hz   = (int)cfg->freq_hz,
        .input_delay_ns   = 0,
        .spics_io_num     = cfg->cs_pin,
        .flags            = cfg->flags,
        .queue_size       = cfg->queue_size > 0 ? cfg->queue_size : 1,
        .pre_cb           = NULL,
        .post_cb          = NULL,
    };

    /* 处理 flags 的 bit 位映射 */
    if (cfg->flags & PAL_SPI_FLAG_HALF_DUPLEX) {
        dev_cfg.flags |= SPI_DEVICE_HALFDUPLEX;
    }
    if (cfg->flags & PAL_SPI_FLAG_CS_ACTIVE_HIGH) {
        dev_cfg.flags |= SPI_DEVICE_POSITIVE_CS;
    }
    if (cfg->flags & PAL_SPI_FLAG_3WIRE) {
        dev_cfg.flags |= SPI_DEVICE_3WIRE;
    }
    if (cfg->flags & PAL_SPI_FLAG_NO_DMA) {
        dev_cfg.flags |= SPI_DEVICE_NO_DUMMY;
    }
    if (cfg->lsb_first) {
        dev_cfg.flags |= SPI_DEVICE_BIT_LSBFIRST;
    }

    esp_err_t ret = spi_bus_add_device(bus_int->host, &dev_cfg,
                                       &dev_int->dev_handle);
    if (ret != ESP_OK) {
        free(dev_int);
        return ret;
    }

    dev_int->bus = bus;
    *dev = (pal_spi_dev_handle_t)dev_int;
    return ESP_OK;
}

int pal_spi_dev_detach(pal_spi_dev_handle_t dev)
{
    pal_spi_dev_internal_t *dev_int = (pal_spi_dev_internal_t *)dev;
    if (dev_int == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = spi_bus_remove_device(dev_int->dev_handle);
    free(dev_int);
    return ret;
}

/* ================================================================
 *  数据传输
 * ================================================================ */

int pal_spi_transfer(pal_spi_dev_handle_t dev, const uint8_t *tx_data,
                     uint8_t *rx_data, size_t len)
{
    pal_spi_dev_internal_t *dev_int = (pal_spi_dev_internal_t *)dev;
    if (dev_int == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t trans = {
        .length    = len * 8,
        .rxlength  = rx_data ? len * 8 : 0,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
        .flags     = 0,
    };

    return spi_device_transmit(dev_int->dev_handle, &trans);
}

int pal_spi_transmit(pal_spi_dev_handle_t dev, const uint8_t *data,
                     size_t len)
{
    /* 仅发送，忽略接收 */
    return pal_spi_transfer(dev, data, NULL, len);
}

int pal_spi_transmit_receive(pal_spi_dev_handle_t dev,
                             const uint8_t *cmd, size_t cmd_len,
                             uint8_t *rx, size_t rx_len)
{
    pal_spi_dev_internal_t *dev_int = (pal_spi_dev_internal_t *)dev;
    if (dev_int == NULL || cmd == NULL || rx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 分配合并缓冲：cmd + dummy rx */
    size_t total_len = cmd_len + rx_len;
    uint8_t *tx_buf = calloc(1, total_len);
    if (tx_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    uint8_t *rx_buf = calloc(1, total_len);
    if (rx_buf == NULL) {
        free(tx_buf);
        return ESP_ERR_NO_MEM;
    }

    memcpy(tx_buf, cmd, cmd_len);
    /* 后半段 tx 为 0x00（或 0xFF），由 calloc 已清零 */

    spi_transaction_t trans = {
        .length    = total_len * 8,
        .rxlength  = total_len * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };

    esp_err_t ret = spi_device_transmit(dev_int->dev_handle, &trans);

    if (ret == ESP_OK) {
        memcpy(rx, rx_buf + cmd_len, rx_len);
    }

    free(tx_buf);
    free(rx_buf);
    return ret;
}

int pal_spi_exchange(pal_spi_dev_handle_t dev, const uint8_t *tx_data,
                     uint8_t *rx_data, size_t len)
{
    return pal_spi_transfer(dev, tx_data, rx_data, len);
}
