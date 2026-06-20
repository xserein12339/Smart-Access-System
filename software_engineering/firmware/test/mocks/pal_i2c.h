/**
 * @file    pal_i2c.h
 * @brief   Mock pal_i2c.h — PAL I2C 接口（宿主机测试用）
 *
 * @details 拦截被测 BSP 对真实 PAL I2C 的依赖。用 FFF 生成可控假函数，
 *          便于测试设返回值、检查 I2C 写入的寄存器/数据。
 *          仅 mock BSP 实际使用的子集（dev_attach/detach/write/read/
 *          read_reg/write_reg_byte）。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef MOCK_PAL_I2C_H
#define MOCK_PAL_I2C_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "fff.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *pal_i2c_bus_handle_t;
typedef void *pal_i2c_dev_handle_t;

/** I2C 设备挂载配置（与真实 pal_i2c.h 字段一致） */
typedef struct {
    uint16_t device_address;
    uint32_t scl_speed_hz;
    bool     disable_ack_check;
} pal_i2c_dev_config_t;

/* ================================================================
 *  FFF Fake 函数
 * ================================================================ */
#ifdef FFF_MOCK_DEFINITIONS
FAKE_VALUE_FUNC3(int, pal_i2c_dev_attach, pal_i2c_dev_handle_t *, pal_i2c_bus_handle_t, const pal_i2c_dev_config_t *);
FAKE_VALUE_FUNC1(int, pal_i2c_dev_detach, pal_i2c_dev_handle_t);
FAKE_VALUE_FUNC3(int, pal_i2c_write, pal_i2c_dev_handle_t, const uint8_t *, size_t);
FAKE_VALUE_FUNC3(int, pal_i2c_read, pal_i2c_dev_handle_t, uint8_t *, size_t);
FAKE_VALUE_FUNC4(int, pal_i2c_read_reg, pal_i2c_dev_handle_t, uint8_t, uint8_t *, size_t);
FAKE_VALUE_FUNC3(int, pal_i2c_write_reg_byte, pal_i2c_dev_handle_t, uint8_t, uint8_t);
#else
DECLARE_FAKE_VALUE_FUNC3(int, pal_i2c_dev_attach, pal_i2c_dev_handle_t *, pal_i2c_bus_handle_t, const pal_i2c_dev_config_t *);
DECLARE_FAKE_VALUE_FUNC1(int, pal_i2c_dev_detach, pal_i2c_dev_handle_t);
DECLARE_FAKE_VALUE_FUNC3(int, pal_i2c_write, pal_i2c_dev_handle_t, const uint8_t *, size_t);
DECLARE_FAKE_VALUE_FUNC3(int, pal_i2c_read, pal_i2c_dev_handle_t, uint8_t *, size_t);
DECLARE_FAKE_VALUE_FUNC4(int, pal_i2c_read_reg, pal_i2c_dev_handle_t, uint8_t, uint8_t *, size_t);
DECLARE_FAKE_VALUE_FUNC3(int, pal_i2c_write_reg_byte, pal_i2c_dev_handle_t, uint8_t, uint8_t);
#endif /* FFF_MOCK_DEFINITIONS */

#ifdef __cplusplus
}
#endif
#endif /* MOCK_PAL_I2C_H */
