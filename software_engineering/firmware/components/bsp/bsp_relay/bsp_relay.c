/**
 * @file    bsp_relay.c
 * @brief   Board V1 继电器 BSP — GPIO 后端实现 + 自注册到 DAL
 *
 * @details 封装 PAL GPIO 原语驱动本板继电器，实现 dal_relay_ops_t 契约，
 *          并在 bsp_relay_init() 中以业务语义名称自注册到 DAL。
 *          PAL 返回码（int/esp_err_t 兼容）在本文件的 ops 边界翻译为
 *          dal_err_t，不透传到上层。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */
#include "bsp_relay.h"
#include "dal_relay.h"            /* DAL 管理 API（register） */
#include "dal_relay_interface.h"  /* relay 接口契约 */
#include "pal_gpio.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

/* ================================================================
 *  PAL 错误码 → dal_err_t 翻译（BSP 边界）
 * ================================================================ */

/**
 * @brief PAL int 返回码翻译为 dal_err_t
 * @note  PAL 约定：0 成功，负数失败（esp_err_t 兼容码）。
 *        此处仅区分「参数非法」与「一般硬件错误」，具体 esp_err_t
 *        不向上层泄露。
 */
static dal_err_t relay_err_from_pal(int pal_ret)
{
    if (pal_ret == 0) {
        return DAL_OK;
    }
    if (pal_ret == ESP_ERR_INVALID_ARG) {
        return DAL_ERR_INVALID;
    }
    return DAL_ERR_HW;
}

/* ================================================================
 *  GPIO 继电器硬件上下文（BSP 私有）
 * ================================================================ */
typedef struct {
    int  gpio_pin;     /**< GPIO 引脚号（仅 BSP 知道） */
    bool active_high;  /**< true=高电平吸合, false=低电平吸合 */
} bsp_relay_hw_ctx_t;

/* ================================================================
 *  GPIO 继电器 ops 实现（BSP 私有）
 * ================================================================ */

/** @brief 设置继电器开关 */
static dal_err_t bsp_relay_gpio_set(void *ctx, bool on)
{
    const bsp_relay_hw_ctx_t *hw = (const bsp_relay_hw_ctx_t *)ctx;
    int level = on ? (hw->active_high ? 1 : 0)
                   : (hw->active_high ? 0 : 1);
    return relay_err_from_pal(pal_gpio_write(hw->gpio_pin, level));
}

/** @brief 读取继电器当前状态 */
static dal_err_t bsp_relay_gpio_get(void *ctx, bool *out)
{
    const bsp_relay_hw_ctx_t *hw = (const bsp_relay_hw_ctx_t *)ctx;
    if (out == NULL) {
        return DAL_ERR_INVALID;
    }

    int level = pal_gpio_read(hw->gpio_pin);
    if (level < 0) {
        return relay_err_from_pal(level);
    }

    *out = hw->active_high ? (level == 1) : (level == 0);
    return DAL_OK;
}

/* BSP 私有的 GPIO ops，不对外暴露 */
static const dal_relay_ops_t s_gpio_relay_ops = {
    .set = bsp_relay_gpio_set,
    .get = bsp_relay_gpio_get,
};

/* ================================================================
 *  本板硬件实例（仅此处知道引脚号与有效电平）
 * ================================================================ */
static bsp_relay_hw_ctx_t s_lock_ctx    = { .gpio_pin = 1, .active_high = false };
static bsp_relay_hw_ctx_t s_alarm_ctx   = { .gpio_pin = 2, .active_high = false };
static bsp_relay_hw_ctx_t s_wiegand_ctx = { .gpio_pin = 3, .active_high = true  };

/* ================================================================
 *  BSP 对外初始化接口（自注册）
 * ================================================================ */
dal_err_t bsp_relay_init(void)
{
    /* 1. GPIO 硬件初始化 + 置安全默认态（断开） */
    const bsp_relay_hw_ctx_t *all_ctx[] = { &s_lock_ctx, &s_alarm_ctx, &s_wiegand_ctx };
    for (size_t i = 0; i < sizeof(all_ctx) / sizeof(all_ctx[0]); i++) {
        if (pal_gpio_set_direction(all_ctx[i]->gpio_pin, PAL_GPIO_DIR_OUTPUT) != 0) {
            return DAL_ERR_HW;
        }
        int off_level = all_ctx[i]->active_high ? 0 : 1;
        if (pal_gpio_write(all_ctx[i]->gpio_pin, off_level) != 0) {
            return DAL_ERR_HW;
        }
    }

    /* 2. 以业务语义名称自注册到 DAL */
    dal_err_t ret;
    ret = dal_relay_register("door_lock",   &s_gpio_relay_ops, &s_lock_ctx);
    if (ret != DAL_OK) return ret;

    ret = dal_relay_register("alarm",       &s_gpio_relay_ops, &s_alarm_ctx);
    if (ret != DAL_OK) return ret;

    ret = dal_relay_register("wiegand_pwr", &s_gpio_relay_ops, &s_wiegand_ctx);
    if (ret != DAL_OK) return ret;

    return DAL_OK;
}
