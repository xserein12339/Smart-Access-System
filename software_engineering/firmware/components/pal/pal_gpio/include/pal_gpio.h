/**
 * @file    pal_gpio.h
 * @brief   PAL GPIO 模块 — 引脚控制与外部中断
 *
 * 封装 ESP-IDF driver/gpio.h，提供统一的引脚输入/输出配置、
 * 电平读写、内部上下拉控制以及外部中断注册接口。
 * 所有函数返回 0 表示成功，负数为 esp_err_t 兼容错误码。
 *
 * @note   ISR 回调运行在中断上下文中，仅允许做标志位置位或队列发送，
 *         禁止 printf、阻塞操作等耗时行为。
 *
 * 参考文档：ESP32-P4 TRM GPIO 章节
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef PAL_GPIO_H
#define PAL_GPIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  枚举类型
 * ================================================================ */

/** @brief 引脚方向 */
typedef enum {
    PAL_GPIO_DIR_INPUT         = 0,   /**< 输入模式 */
    PAL_GPIO_DIR_OUTPUT        = 1,   /**< 推挽输出 */
    PAL_GPIO_DIR_INPUT_OUTPUT  = 2,   /**< 开漏输出 + 输入 */
} pal_gpio_dir_t;

/** @brief 内部上/下拉模式 */
typedef enum {
    PAL_GPIO_PULL_NONE = 0,   /**< 无上下拉 */
    PAL_GPIO_PULL_UP   = 1,   /**< 内部上拉 */
    PAL_GPIO_PULL_DOWN = 2,   /**< 内部下拉 */
} pal_gpio_pull_t;

/** @brief GPIO 中断触发类型 */
typedef enum {
    PAL_GPIO_INTR_DISABLE     = 0,   /**< 禁用中断 */
    PAL_GPIO_INTR_POSEDGE     = 1,   /**< 上升沿触发 */
    PAL_GPIO_INTR_NEGEDGE     = 2,   /**< 下降沿触发 */
    PAL_GPIO_INTR_ANYEDGE     = 3,   /**< 双边沿触发 */
    PAL_GPIO_INTR_LOW_LEVEL   = 4,   /**< 低电平触发 */
    PAL_GPIO_INTR_HIGH_LEVEL  = 5,   /**< 高电平触发 */
} pal_gpio_intr_edge_t;

/* ================================================================
 *  ISR 回调类型
 * ================================================================ */

/**
 * @brief GPIO 中断回调函数原型
 *
 * @param arg 注册时传入的用户参数指针
 *
 * @warning 回调运行于 ISR 上下文，务必保持简短；
 *          只做标志位操作 / 队列发送，禁止 printf、阻塞操作。
 */
typedef void (*pal_gpio_isr_cb_t)(void *arg);

/* ================================================================
 *  引脚配置 API
 * ================================================================ */

/**
 * @brief 设置引脚方向
 *
 * @param pin GPIO 引脚号
 * @param dir 方向模式
 * @return 0 成功，负数失败
 */
int pal_gpio_set_direction(int pin, pal_gpio_dir_t dir);

/**
 * @brief 设置引脚内部上/下拉
 *
 * @param pin  GPIO 引脚号
 * @param pull 上/下拉模式
 * @return 0 成功，负数失败
 */
int pal_gpio_set_pull_mode(int pin, pal_gpio_pull_t pull);

/**
 * @brief 设置输出驱动强度
 *
 * @param pin      GPIO 引脚号
 * @param strength 驱动强度级别（0 ≈ 5 mA，1 ≈ 10 mA，2 ≈ 20 mA，3 ≈ 40 mA）
 * @return 0 成功，负数失败
 */
int pal_gpio_set_drive_strength(int pin, uint8_t strength);

/* ================================================================
 *  输出 API
 * ================================================================ */

/**
 * @brief 设置引脚输出电平
 *
 * @param pin   GPIO 引脚号
 * @param level 0 = 低电平，1 = 高电平
 * @return 0 成功
 */
int pal_gpio_write(int pin, int level);

/**
 * @brief 翻转引脚输出电平
 *
 * @param pin GPIO 引脚号
 * @return 0 成功
 */
int pal_gpio_toggle(int pin);

/**
 * @brief 按掩码同时设置多个引脚电平
 *
 * @param pin_mask 引脚位掩码，如 BIT(4) | BIT(5)
 * @param val_mask 对应电平掩码（1=高，0=低）
 * @return 0 成功
 */
int pal_gpio_write_mask(uint64_t pin_mask, uint64_t val_mask);

/* ================================================================
 *  输入 API
 * ================================================================ */

/**
 * @brief 读取引脚输入电平
 *
 * @param pin GPIO 引脚号
 * @return 0 或 1，负数表示错误
 */
int pal_gpio_read(int pin);

/**
 * @brief 按掩码读取多引脚电平
 *
 * @param pin_mask 引脚位掩码
 * @return 对应电平位掩码（1=高，0=低），负数表示错误
 */
int64_t pal_gpio_read_mask(uint64_t pin_mask);

/* ================================================================
 *  中断 API
 * ================================================================ */

/**
 * @brief 安装全局 GPIO ISR 服务（仅需调用一次）
 *
 * 后续 pal_gpio_set_intr() 复用已安装的服务。
 * 需在注册任何中断回调前调用。
 *
 * @param intr_alloc_flags 中断分配标志（如 ESP_INTR_FLAG_IRAM）
 * @return 0 成功，负数失败
 */
int pal_gpio_install_isr_service(int intr_alloc_flags);

/**
 * @brief 卸载全局 GPIO ISR 服务
 */
void pal_gpio_uninstall_isr_service(void);

/**
 * @brief 配置引脚中断并注册回调
 *
 * @param pin  GPIO 引脚号
 * @param edge 触发类型
 * @param cb   中断回调函数（ISR 上下文执行）
 * @param arg  传递给回调的用户参数指针
 * @return 0 成功，负数失败
 */
int pal_gpio_set_intr(int pin, pal_gpio_intr_edge_t edge,
                      pal_gpio_isr_cb_t cb, void *arg);

/**
 * @brief 使能已配置的 GPIO 中断
 *
 * @param pin GPIO 引脚号
 * @return 0 成功
 */
int pal_gpio_intr_enable(int pin);

/**
 * @brief 禁用 GPIO 中断（保留配置）
 *
 * @param pin GPIO 引脚号
 * @return 0 成功
 */
int pal_gpio_intr_disable(int pin);

/**
 * @brief 移除引脚中断并禁用
 *
 * @param pin GPIO 引脚号
 * @return 0 成功
 */
int pal_gpio_remove_intr(int pin);

#ifdef __cplusplus
}
#endif

#endif /* PAL_GPIO_H */
