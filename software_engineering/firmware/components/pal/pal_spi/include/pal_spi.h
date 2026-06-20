/**
 * @file    pal_spi.h
 * @brief   PAL SPI 模块 — SPI 主机通信
 *
 * 封装 ESP-IDF driver/spi_master.h，提供：
 *   - SPI 总线初始化 / 反初始化
 *   - 设备挂载（指定 CS、频率、模式）
 *   - 全双工传输 / 单工收发
 *
 * 仅支持主机模式（Master）。
 *
 * 参考文档：ESP32-P4 TRM SPI 章节
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef PAL_SPI_H
#define PAL_SPI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief SPI 总线不透明句柄 */
typedef void *pal_spi_bus_handle_t;

/** @brief SPI 设备不透明句柄 */
typedef void *pal_spi_dev_handle_t;

/* ================================================================
 *  枚举类型
 * ================================================================ */

/** @brief SPI 主机编号 */
typedef enum {
    PAL_SPI_HOST_1 = 1,   /**< SPI1（内部 Flash 使用，慎用） */
    PAL_SPI_HOST_2 = 2,   /**< SPI2（通用 GPSPI） */
    PAL_SPI_HOST_3 = 3,   /**< SPI3（通用 GPSPI） */
} pal_spi_host_t;

/** @brief SPI 模式（CPOL + CPHA） */
typedef enum {
    PAL_SPI_MODE_0 = 0,   /**< CPOL=0, CPHA=0 */
    PAL_SPI_MODE_1 = 1,   /**< CPOL=0, CPHA=1 */
    PAL_SPI_MODE_2 = 2,   /**< CPOL=1, CPHA=0 */
    PAL_SPI_MODE_3 = 3,   /**< CPOL=1, CPHA=1 */
} pal_spi_mode_t;

/** @brief SPI 设备标志位 */
typedef enum {
    PAL_SPI_FLAG_NONE          = 0,        /**< 无特殊标志 */
    PAL_SPI_FLAG_CS_ACTIVE_HIGH = (1 << 0), /**< CS 高有效（默认低有效） */
    PAL_SPI_FLAG_HALF_DUPLEX   = (1 << 1), /**< 半双工模式 */
    PAL_SPI_FLAG_3WIRE          = (1 << 2), /**< 三线模式（MOSI/MISO 复用） */
    PAL_SPI_FLAG_NO_DMA         = (1 << 3), /**< 禁用 DMA */
} pal_spi_dev_flag_t;

/* ================================================================
 *  总线 API
 * ================================================================ */

/**
 * @brief SPI 总线初始化配置
 */
typedef struct {
    int             host;              /**< SPI 主机编号 */
    int             mosi_pin;          /**< MOSI 引脚，-1 不使用 */
    int             miso_pin;          /**< MISO 引脚，-1 不使用 */
    int             sclk_pin;          /**< SCLK 引脚 */
    int             quadwp_pin;        /**< WP 引脚（Quad SPI），-1 不使用 */
    int             quadhd_pin;        /**< HD 引脚（Quad SPI），-1 不使用 */
    int             max_transfer_sz;   /**< 最大传输字节数（DMA 限制，默认 4096） */
    uint32_t        flags;             /**< SPI 总线标志 */
    int             intr_flags;        /**< 中断分配标志 */
} pal_spi_bus_config_t;

/**
 * @brief 初始化 SPI 主机总线
 *
 * @param[out] handle 返回的总线句柄
 * @param[in]  cfg    总线配置
 * @return 0 成功，负数失败
 */
int pal_spi_bus_init(pal_spi_bus_handle_t *handle,
                     const pal_spi_bus_config_t *cfg);

/**
 * @brief 反初始化 SPI 总线并释放资源
 *
 * @param handle 总线句柄
 * @return 0 成功
 */
int pal_spi_bus_deinit(pal_spi_bus_handle_t handle);

/* ================================================================
 *  设备 API
 * ================================================================ */

/**
 * @brief SPI 设备挂载配置
 */
typedef struct {
    int         cs_pin;          /**< 片选引脚 */
    uint32_t    freq_hz;         /**< 时钟频率（Hz） */
    pal_spi_mode_t mode;         /**< SPI 模式（CPOL/CPHA） */
    uint8_t     flags;           /**< 设备标志位（pal_spi_dev_flag_t 组合） */
    int         queue_size;      /**< 传输队列深度 */
    int         pre_cb_latency_us; /**< CS 有效到时钟开始之间的延时（us） */
    int         post_cb_latency_us;/**< 时钟结束到 CS 无效之间的延时（us） */
    int         cs_ena_pretrans;   /**< CS 建立时间（SPI 时钟周期数） */
    int         cs_ena_posttrans;  /**< CS 保持时间（SPI 时钟周期数） */
    bool        lsb_first;         /**< LSB 先发（默认 MSB） */
} pal_spi_dev_config_t;

/**
 * @brief 在总线上挂载 SPI 设备，获取设备句柄
 *
 * @param[out] dev 返回的设备句柄
 * @param      bus 总线句柄
 * @param      cfg 设备配置
 * @return 0 成功，负数失败
 */
int pal_spi_dev_attach(pal_spi_dev_handle_t *dev, pal_spi_bus_handle_t bus,
                       const pal_spi_dev_config_t *cfg);

/**
 * @brief 从总线移除设备并释放句柄
 *
 * @param dev 设备句柄
 * @return 0 成功
 */
int pal_spi_dev_detach(pal_spi_dev_handle_t dev);

/* ================================================================
 *  传输 API
 * ================================================================ */

/**
 * @brief SPI 全双工传输（同时发和收）
 *
 * @param dev     设备句柄
 * @param tx_data 发送缓冲区（可为 NULL，发送 0xFF 占位）
 * @param[out] rx_data 接收缓冲区（可为 NULL，丢弃接收数据）
 * @param len     传输字节数
 * @return 0 成功，负数失败
 */
int pal_spi_transfer(pal_spi_dev_handle_t dev, const uint8_t *tx_data,
                     uint8_t *rx_data, size_t len);

/**
 * @brief SPI 仅发送（忽略 MISO 数据）
 *
 * @param dev  设备句柄
 * @param data 发送数据缓冲区
 * @param len  发送字节数
 * @return 0 成功
 */
int pal_spi_transmit(pal_spi_dev_handle_t dev, const uint8_t *data,
                     size_t len);

/**
 * @brief SPI 发送命令+地址再接收数据（适合 Flash/SD 等命令式设备）
 *
 * 发送 cmd_buf + addr_buf，然后接收 rx_data。
 *
 * @param dev      设备句柄
 * @param cmd      命令字节序列
 * @param cmd_len  命令长度
 * @param[out] rx  接收缓冲区
 * @param rx_len   接收字节数
 * @return 0 成功
 */
int pal_spi_transmit_receive(pal_spi_dev_handle_t dev,
                             const uint8_t *cmd, size_t cmd_len,
                             uint8_t *rx, size_t rx_len);

/**
 * @brief SPI 同时发送和接收（全双工），使用独立收发缓冲区
 *
 * @param dev      设备句柄
 * @param tx_data  发送缓冲区
 * @param[out] rx_data 接收缓冲区
 * @param len      收发字节数
 * @return 0 成功
 */
int pal_spi_exchange(pal_spi_dev_handle_t dev, const uint8_t *tx_data,
                     uint8_t *rx_data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PAL_SPI_H */
