/**
 * @file    pal_i2c.h
 * @brief   PAL I2C 模块 — I2C 主机通信
 *
 * 封装 ESP-IDF driver/i2c_master.h（新版驱动 API），提供：
 *   - I2C 总线初始化 / 反初始化
 *   - 设备探测与挂载
 *   - 寄存器读写（单字节 / 多字节地址）
 *   - 原始数据收发
 *
 * 仅支持主机模式（Master）。
 *
 * 参考文档：ESP32-P4 TRM I2C 章节
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef PAL_I2C_H
#define PAL_I2C_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief I2C 总线不透明句柄 */
typedef void *pal_i2c_bus_handle_t;

/** @brief I2C 设备不透明句柄 */
typedef void *pal_i2c_dev_handle_t;

/* ================================================================
 *  总线 API
 * ================================================================ */

/**
 * @brief I2C 总线初始化配置
 */
typedef struct {
    int      port;                  /**< I2C 端口号，-1 表示自动选择 */
    int      sda_pin;               /**< SDA 引脚号 */
    int      scl_pin;               /**< SCL 引脚号 */
    uint32_t freq_hz;               /**< 总线时钟频率（Hz），标准 100000，快速 400000 */
    bool     enable_internal_pullup;/**< 是否启用内部上拉（建议外部上拉电阻） */
    int      intr_priority;         /**< 中断优先级，0 表示使用默认 */
    size_t   trans_queue_depth;     /**< 内部传输队列深度 */
} pal_i2c_bus_config_t;

/**
 * @brief 初始化 I2C 主机总线
 *
 * @param[out] handle 返回的总线句柄
 * @param[in]  cfg    总线配置
 * @return 0 成功，负数失败
 */
int pal_i2c_bus_init(pal_i2c_bus_handle_t *handle,
                     const pal_i2c_bus_config_t *cfg);

/**
 * @brief 反初始化 I2C 主机总线并释放资源
 *
 * @param handle 总线句柄
 * @return 0 成功
 */
int pal_i2c_bus_deinit(pal_i2c_bus_handle_t handle);

/* ================================================================
 *  设备 API
 * ================================================================ */

/**
 * @brief I2C 设备挂载配置
 */
typedef struct {
    uint16_t      device_address;   /**< 设备 7/10 位地址（不含读写位） */
    uint32_t      scl_speed_hz;     /**< 该设备的 SCL 频率（Hz），0 使用总线默认 */
    bool          disable_ack_check;/**< 是否禁用 ACK 检查（非标准设备） */
} pal_i2c_dev_config_t;

/**
 * @brief 探测 I2C 设备是否存在（发送地址并检测 ACK）
 *
 * @param bus  总线句柄
 * @param addr 7 位设备地址
 * @return 0 设备存在，负数不存在或错误
 */
int pal_i2c_dev_probe(pal_i2c_bus_handle_t bus, uint16_t addr);

/**
 * @brief 在总线上挂载设备，获取设备句柄
 *
 * @param[out] dev 返回的设备句柄
 * @param      bus 总线句柄
 * @param      cfg 设备配置
 * @return 0 成功，负数失败
 */
int pal_i2c_dev_attach(pal_i2c_dev_handle_t *dev, pal_i2c_bus_handle_t bus,
                       const pal_i2c_dev_config_t *cfg);

/**
 * @brief 从总线移除设备并释放句柄
 *
 * @param dev 设备句柄
 * @return 0 成功
 */
int pal_i2c_dev_detach(pal_i2c_dev_handle_t dev);

/* ================================================================
 *  读写 API
 * ================================================================ */

/**
 * @brief 向设备写入数据
 *
 * @param dev  设备句柄
 * @param data 写入数据缓冲区
 * @param len  写入字节数
 * @return 0 成功，负数失败
 */
int pal_i2c_write(pal_i2c_dev_handle_t dev, const uint8_t *data, size_t len);

/**
 * @brief 从设备读取数据
 *
 * @param dev  设备句柄
 * @param[out] data 读取数据缓冲区
 * @param len  读取字节数
 * @return 0 成功，负数失败
 */
int pal_i2c_read(pal_i2c_dev_handle_t dev, uint8_t *data, size_t len);

/**
 * @brief 向设备指定寄存器写入数据（先发寄存器地址，再发数据）
 *
 * 操作序列：START → 设备地址+W → 寄存器地址 → 数据… → STOP
 *
 * @param dev      设备句柄
 * @param reg_addr 寄存器地址
 * @param data     写入数据缓冲区
 * @param len      写入字节数
 * @return 0 成功，负数失败
 */
int pal_i2c_write_reg(pal_i2c_dev_handle_t dev, uint8_t reg_addr,
                      const uint8_t *data, size_t len);

/**
 * @brief 从设备指定寄存器读取数据（先发寄存器地址，再收数据）
 *
 * 操作序列：START → 设备地址+W → 寄存器地址 → RESTART → 设备地址+R → 数据… → STOP
 *
 * @param dev      设备句柄
 * @param reg_addr 寄存器地址
 * @param[out] data 读取数据缓冲区
 * @param len      读取字节数
 * @return 0 成功，负数失败
 */
int pal_i2c_read_reg(pal_i2c_dev_handle_t dev, uint8_t reg_addr,
                     uint8_t *data, size_t len);

/**
 * @brief 向设备寄存器写入单个字节（快捷方式）
 *
 * @param dev  设备句柄
 * @param reg  寄存器地址
 * @param val  写入值
 * @return 0 成功
 */
int pal_i2c_write_reg_byte(pal_i2c_dev_handle_t dev, uint8_t reg, uint8_t val);

/**
 * @brief 从设备寄存器读取单个字节（快捷方式）
 *
 * @param dev  设备句柄
 * @param reg  寄存器地址
 * @param[out] val 读取值
 * @return 0 成功
 */
int pal_i2c_read_reg_byte(pal_i2c_dev_handle_t dev, uint8_t reg, uint8_t *val);

/**
 * @brief 获取底层 I2C 主机总线句柄（供 esp_video 等外部框架复用总线）
 *
 * @param handle  PAL I2C 总线句柄
 * @return 底层 i2c_master_bus_handle_t（可直接传给 esp_video_init_sccb_config_t.i2c_handle）
 *
 * @note 调用方需 #include "driver/i2c_master.h" 以获知实际类型
 */
void *pal_i2c_get_bus_handle(pal_i2c_bus_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* PAL_I2C_H */
