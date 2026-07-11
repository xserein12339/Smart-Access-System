/**
 * @file    bsp_display_attiny88.h
 * @brief   ATTINY88 背光/电源管理 MCU 子驱动（BSP 内部，不放入 include/）
 *
 * @details ATTINY88 通过 I2C（地址 0x45）控制树莓派 7 寸屏的：
 *            - 面板主电源（PORTB bit7）
 *            - TC358762 桥 / LCD / 触控 复位（PORTC）
 *            - 背光 PWM（0~255）
 *          上电序列：PORTB 开主电源 → PORTC 释放复位 → 设背光。
 *          仅 bsp_display_rpi7pin.c 聚合层可包含本文件。
 *
 *          直接使用 ESP-IDF driver/i2c_master.h 原生 API（i2c_master_bus_add_device
 *          / i2c_master_transmit / i2c_master_transmit_receive），不再经 pal_i2c 封装。
 *          共享 I2C 总线由 board_v1 提供（board_i2c_get_bus 返回原生
 *          i2c_master_bus_handle_t）。
 *
 *          寄存器参考：树莓派 7" DSI 屏 ATTINY88 固件 v2 (ID=0xC3)
 *
 * @author  xLumina
 * @version 2.0
 */
#ifndef BSP_DISPLAY_ATTINY88_H
#define BSP_DISPLAY_ATTINY88_H

#include "dal_err.h"
#include "driver/i2c_master.h"      /* i2c_master_bus_handle_t / i2c_master_dev_handle_t */
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 寄存器地址 ---- */
#define ATTINY88_REG_ID      0x80   /**< 固件 ID (RO, 0xC3=v2) */
#define ATTINY88_REG_PORTA   0x81   /**< 扫描方向 */
#define ATTINY88_REG_PORTB   0x82   /**< 主电源 */
#define ATTINY88_REG_PORTC   0x83   /**< 复位控制 */
#define ATTINY88_REG_PWM     0x86   /**< 背光 PWM (0~255) */

/* ---- PORTA 位 ---- */
#define ATTINY88_PORTA_SCAN_LR    (1 << 2)   /**< 扫描方向：左→右 */
/* ---- PORTB 位 ---- */
#define ATTINY88_PORTB_POWER_ON   (1 << 7)   /**< 主电源开关 */
/* ---- PORTC 复位控制位（低有效，置 1 = 释放复位）---- */
#define ATTINY88_PORTC_LED_EN     (1 << 0)   /**< 背光 LED 使能 */
#define ATTINY88_PORTC_TOUCH_RST  (1 << 1)   /**< 触控复位 */
#define ATTINY88_PORTC_LCD_RST    (1 << 2)   /**< LCD 复位 */
#define ATTINY88_PORTC_BRIDGE_RST (1 << 3)   /**< TC358762 桥复位 */

/** 固件 ID */
#define ATTINY88_FW_ID_V2   0xC3

/** ATTINY88 驱动上下文（BSP 私有） */
typedef struct {
    i2c_master_dev_handle_t dev;       /**< 原生 I2C 设备句柄 */
    uint8_t                 i2c_addr;  /**< 设备 7 位地址 */
    bool                    inited;    /**< 是否已初始化 */
} bsp_attiny88_ctx_t;

/**
 * @brief 初始化 ATTINY88（在共享 I2C 总线上挂载设备，读 ID 校验）
 *
 * @param[in,out] ctx 上下文（由调用方分配）
 * @param[in]     bus 共享 I2C 总线句柄（board_i2c_get_bus 返回的原生句柄）
 * @return DAL_OK 成功，DAL_ERR_HW I2C 错误，DAL_ERR_INVALID 参数非法
 *
 * @note I2C 地址取自 board_v1_config.h 的 BOARD_DISPLAY_ATTINY88_I2C_ADDR。
 *       初始状态：全部复位保持、背光关闭。读 ID 失败仅告警，不阻断
 *       （某些板可能未烧 v2 固件）。
 */
dal_err_t bsp_attiny88_init(bsp_attiny88_ctx_t *ctx, i2c_master_bus_handle_t bus);

/**
 * @brief 反初始化（关电源/背光 + 移除 I2C 设备）
 * @param[in] ctx 上下文
 * @return DAL_OK 成功
 */
dal_err_t bsp_attiny88_deinit(bsp_attiny88_ctx_t *ctx);

/**
 * @brief 主电源上电 + 设置扫描方向 + LED 使能（桥/LCD/触控复位保持）
 *
 * @details 上电序列第一步：开主电源、配置扫描、使能背光 LED，
 *          但桥/LCD/触控仍保持复位，等待 DSI 就绪后再释放。
 *          内部 vTaskDelay 等待电源稳定。
 *
 * @param[in] ctx 上下文
 * @return DAL_OK 成功
 */
dal_err_t bsp_attiny88_power_on(bsp_attiny88_ctx_t *ctx);

/**
 * @brief 释放桥/LCD/触控复位（在 DSI 总线就绪后调用）
 *
 * @details 内部 vTaskDelay 等待桥就绪。
 *
 * @param[in] ctx 上下文
 * @return DAL_OK 成功
 */
dal_err_t bsp_attiny88_release_reset(bsp_attiny88_ctx_t *ctx);

/**
 * @brief 设置背光亮度
 * @param[in] ctx     上下文
 * @param[in] percent 背光百分比 0~100（内部映射到 0~255）
 * @return DAL_OK 成功
 */
dal_err_t bsp_attiny88_set_backlight(bsp_attiny88_ctx_t *ctx, uint8_t percent);

#ifdef __cplusplus
}
#endif
#endif /* BSP_DISPLAY_ATTINY88_H */
