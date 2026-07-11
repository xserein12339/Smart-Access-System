/**
 * @file    bsp_relay.c
 * @brief   Board V1 继电器 BSP — GPIO 后端，ops + ctx 绑定
 *
 * @details 直接调用 ESP-IDF driver/gpio 驱动本板继电器，实现
 *          dal_relay_ops_t 契约。bsp_relay_create() 仅返回静态 ops 指针
 *          （ctx 编译期注入 ops->ctx），不注册 DAL、不驱动硬件；硬件初始化
 *          封装在 ops->init()，由上层按需触发。esp_err_t 在 ops 边界翻译
 *          为 dal_err_t，不透传到上层。
 *
 *          本板 3 个继电器实例各有独立静态 ops + ctx。
 *
 * @author  xiamu
 * @version 1.2
 */
#include "bsp_relay.h"
#include "dal_esp_err.h"
#include "board_v1_config.h"
#include "driver/gpio.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ================================================================
 *  硬件上下文（BSP 私有，仅此处知道引脚号与有效电平）
 * ================================================================ */
typedef struct {
    gpio_num_t pin;        /**< GPIO 引脚号 */
    bool       active_high;/**< true=高电平吸合, false=低电平吸合 */
} bsp_relay_ctx_t;

/* ================================================================
 *  ops 实现（BSP 私有，3 实例共用同一组函数）
 * ================================================================ */

/** @brief 初始化继电器 GPIO（输出模式 + 安全默认态） */
static dal_err_t relay_init(void *ctx)
{
    bsp_relay_ctx_t *c = (bsp_relay_ctx_t *)ctx;
    gpio_config_t cfg = {
        .pin_bit_mask = BIT64(c->pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t e = gpio_config(&cfg);
    if (e != ESP_OK) {
        return dal_err_from_esp(e);
    }
    /* 安全默认态：断开（非有效电平） */
    int off_level = c->active_high ? 0 : 1;
    gpio_set_level(c->pin, off_level);
    return DAL_OK;
}

/** @brief 设置继电器开关状态 */
static dal_err_t relay_set(void *ctx, bool on)
{
    bsp_relay_ctx_t *c = (bsp_relay_ctx_t *)ctx;
    int level = on ? (c->active_high ? 1 : 0)
                   : (c->active_high ? 0 : 1);
    return dal_err_from_esp(gpio_set_level(c->pin, level));
}

/** @brief 读取继电器当前状态 */
static dal_err_t relay_get(void *ctx, bool *out)
{
    bsp_relay_ctx_t *c = (bsp_relay_ctx_t *)ctx;
    if (out == NULL) {
        return DAL_ERR_INVALID;
    }
    int level = gpio_get_level(c->pin);
    *out = c->active_high ? (level == 1) : (level == 0);
    return DAL_OK;
}

/** @brief 反初始化（GPIO 无需显式释放，保留接口完整性） */
static dal_err_t relay_deinit(void *ctx)
{
    (void)ctx;
    return DAL_OK;
}

/* ================================================================
 *  各实例静态 ctx + 静态 ops（ctx 编译期注入 ops->ctx）
 * ================================================================ */
static bsp_relay_ctx_t s_door_ctx    = { .pin = BOARD_RELAY_DOOR_PIN,    .active_high = BOARD_RELAY_ACTIVE_HIGH };
static bsp_relay_ctx_t s_alarm_ctx   = { .pin = BOARD_RELAY_ALARM_PIN,   .active_high = BOARD_RELAY_ACTIVE_HIGH };
static bsp_relay_ctx_t s_wiegand_ctx = { .pin = BOARD_RELAY_WIEGAND_PIN, .active_high = BOARD_RELAY_ACTIVE_HIGH };

static dal_relay_ops_t s_door_ops = {
    .init = relay_init, .set = relay_set, .get = relay_get, .deinit = relay_deinit,
    .ctx  = &s_door_ctx,
};
static dal_relay_ops_t s_alarm_ops = {
    .init = relay_init, .set = relay_set, .get = relay_get, .deinit = relay_deinit,
    .ctx  = &s_alarm_ctx,
};
static dal_relay_ops_t s_wiegand_ops = {
    .init = relay_init, .set = relay_set, .get = relay_get, .deinit = relay_deinit,
    .ctx  = &s_wiegand_ctx,
};

/* ================================================================
 *  对外 create 入口（仅返回 ops 指针，零副作用）
 * ================================================================ */
dal_relay_ops_t *bsp_relay_create(const char *instance)
{
    if (instance == NULL) {
        return NULL;
    }
    if (strcmp(instance, "door_lock") == 0) {
        return &s_door_ops;
    }
    if (strcmp(instance, "alarm") == 0) {
        return &s_alarm_ops;
    }
    if (strcmp(instance, "wiegand_pwr") == 0) {
        return &s_wiegand_ops;
    }
    return NULL;
}
