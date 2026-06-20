/**
 * @file    pal_gpio.c
 * @brief   PAL GPIO 模块 - 实现（ESP-IDF gpio 驱动封装）
 */

#include "pal_gpio.h"

#include "driver/gpio.h"
#include "esp_err.h"

#include <stddef.h>

/* ================================================================
 *  内部映射函数
 * ================================================================ */

/** @brief PAL 方向枚举 → ESP-IDF gpio_mode_t */
static gpio_mode_t pal_to_esp_mode(pal_gpio_dir_t dir)
{
    switch (dir) {
    case PAL_GPIO_DIR_INPUT:
        return GPIO_MODE_INPUT;
    case PAL_GPIO_DIR_OUTPUT:
        return GPIO_MODE_OUTPUT;
    case PAL_GPIO_DIR_INPUT_OUTPUT:
        return GPIO_MODE_INPUT_OUTPUT;
    default:
        return GPIO_MODE_INPUT;
    }
}

/** @brief PAL 上下拉枚举 → ESP-IDF gpio_pull_mode_t */
static gpio_pull_mode_t pal_to_esp_pull(pal_gpio_pull_t pull)
{
    switch (pull) {
    case PAL_GPIO_PULL_NONE:
        return GPIO_FLOATING;
    case PAL_GPIO_PULL_UP:
        return GPIO_PULLUP_ONLY;
    case PAL_GPIO_PULL_DOWN:
        return GPIO_PULLDOWN_ONLY;
    default:
        return GPIO_FLOATING;
    }
}

/** @brief PAL 中断触发枚举 → ESP-IDF gpio_int_type_t */
static gpio_int_type_t pal_to_esp_intr(pal_gpio_intr_edge_t edge)
{
    switch (edge) {
    case PAL_GPIO_INTR_DISABLE:
        return GPIO_INTR_DISABLE;
    case PAL_GPIO_INTR_POSEDGE:
        return GPIO_INTR_POSEDGE;
    case PAL_GPIO_INTR_NEGEDGE:
        return GPIO_INTR_NEGEDGE;
    case PAL_GPIO_INTR_ANYEDGE:
        return GPIO_INTR_ANYEDGE;
    case PAL_GPIO_INTR_LOW_LEVEL:
        return GPIO_INTR_LOW_LEVEL;
    case PAL_GPIO_INTR_HIGH_LEVEL:
        return GPIO_INTR_HIGH_LEVEL;
    default:
        return GPIO_INTR_DISABLE;
    }
}

/** @brief 全局 ISR 服务是否已安装（单例标记） */
static bool g_isr_service_installed = false;

/* ================================================================
 *  引脚配置
 * ================================================================ */

int pal_gpio_set_direction(int pin, pal_gpio_dir_t dir)
{
    /* 使用 gpio_config 统一设置，保留当前中断/上下拉状态 */
    gpio_config_t cfg = {
        .pin_bit_mask = ((uint64_t)1 << pin),
        .mode         = pal_to_esp_mode(dir),
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

int pal_gpio_set_pull_mode(int pin, pal_gpio_pull_t pull)
{
    gpio_pull_mode_t mode = pal_to_esp_pull(pull);
    return gpio_set_pull_mode(pin, mode);
}

int pal_gpio_set_drive_strength(int pin, uint8_t strength)
{
    return gpio_set_drive_capability(pin, (gpio_drive_cap_t)strength);
}

/* ================================================================
 *  输出
 * ================================================================ */

int pal_gpio_write(int pin, int level)
{
    return gpio_set_level(pin, level ? 1 : 0);
}

int pal_gpio_toggle(int pin)
{
    int cur = gpio_get_level(pin);
    return gpio_set_level(pin, cur ? 0 : 1);
}

int pal_gpio_write_mask(uint64_t pin_mask, uint64_t val_mask)
{
    /* gpio_set_level 为逐引脚接口，此处遍历掩码位逐个操作 */
    for (int i = 0; i < 64; i++) {
        if (pin_mask & ((uint64_t)1 << i)) {
            int level = (val_mask & ((uint64_t)1 << i)) ? 1 : 0;
            gpio_set_level(i, level);
        }
    }
    return ESP_OK;
}

/* ================================================================
 *  输入
 * ================================================================ */

int pal_gpio_read(int pin)
{
    return gpio_get_level(pin);
}

int64_t pal_gpio_read_mask(uint64_t pin_mask)
{
    int64_t result = 0;
    for (int i = 0; i < 64; i++) {
        if (pin_mask & ((uint64_t)1 << i)) {
            if (gpio_get_level(i)) {
                result |= ((int64_t)1 << i);
            }
        }
    }
    return result;
}

/* ================================================================
 *  中断
 * ================================================================ */

int pal_gpio_install_isr_service(int intr_alloc_flags)
{
    if (g_isr_service_installed) {
        return ESP_OK;
    }
    esp_err_t ret = gpio_install_isr_service(intr_alloc_flags);
    if (ret == ESP_OK) {
        g_isr_service_installed = true;
    }
    return ret;
}

void pal_gpio_uninstall_isr_service(void)
{
    if (g_isr_service_installed) {
        gpio_uninstall_isr_service();
        g_isr_service_installed = false;
    }
}

int pal_gpio_set_intr(int pin, pal_gpio_intr_edge_t edge,
                      pal_gpio_isr_cb_t cb, void *arg)
{
    gpio_int_type_t intr_type = pal_to_esp_intr(edge);

    /* 先配置中断触发类型 */
    esp_err_t ret = gpio_set_intr_type(pin, intr_type);
    if (ret != ESP_OK) {
        return ret;
    }

    /* 若禁用中断或回调为空，无需注册 ISR */
    if (edge == PAL_GPIO_INTR_DISABLE || cb == NULL) {
        return ESP_OK;
    }

    ret = gpio_isr_handler_add(pin, (gpio_isr_t)cb, arg);
    return ret;
}

int pal_gpio_intr_enable(int pin)
{
    return gpio_intr_enable(pin);
}

int pal_gpio_intr_disable(int pin)
{
    return gpio_intr_disable(pin);
}

int pal_gpio_remove_intr(int pin)
{
    esp_err_t ret = gpio_intr_disable(pin);
    if (ret != ESP_OK) {
        return ret;
    }
    return gpio_isr_handler_remove(pin);
}
