/**
 * @file    bsp_pir_hcsr501.c
 * @brief   HC-SR501 PIR BSP 实现 — GPIO 双沿中断回调，create + ctx 绑定
 *
 * @details HC-SR501 输出高电平表示检测到人体运动。本驱动为中断回调式：
 *          - init 配置 GPIO 输入 + 下拉，不创建任务。
 *          - set_edge_cb 安装 GPIO 双沿中断（GPIO_INTR_ANYEDGE），ISR 仅调用上层
 *            注册的回调（按当前电平判定上升/下降沿），不在 ISR 内去抖/延时/日志。
 *          - get_state 同步读电平，保留为轮询兜底（自检/调试）。
 *          去抖/离开确认定时器在 service 任务上下文完成（架构约束 + 中断铁律）。
 *          直接调用 ESP-IDF driver/gpio，esp_err_t 在 ops 边界经
 *          dal_err_from_esp() 翻译为 dal_err_t，不透传到上层。
 *
 *          bsp_pir_hcsr501_create() 仅返回静态 ops 指针（ctx 编译期注入
 *          ops->ctx），不注册 DAL、不驱动硬件；硬件初始化封装在 ops->init()，
 *          由上层按需触发。
 *
 * @author  xiamu
 * @version 1.4
 */
#include "bsp_pir_hcsr501.h"
#include "dal_esp_err.h"
#include "board_v1_config.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static const char *PIR_TAG = "PIR";

/* ---- BSP 私有上下文 ---- */
typedef struct {
    gpio_num_t            gpio_pin;
    bool                  inited;
    bool                  intr_enabled;
    dal_pir_edge_cb_t     cb;       /**< 上层边沿回调（ISR 上下文） */
    void                 *user;     /**< 回调用户数据 */
} bsp_pir_ctx_t;

static bsp_pir_ctx_t s_ctx;

/* ================================================================
 *  GPIO ISR：仅判沿并回调，铁律：不日志/不延时/不去抖
 * ================================================================ */

static void IRAM_ATTR pir_gpio_isr_handler(void *arg)
{
    bsp_pir_ctx_t *c = (bsp_pir_ctx_t *)arg;
    if (c == NULL || c->cb == NULL) {
        return;
    }
    /* 读当前电平判定边沿：高=上升沿（人体进入），低=下降沿（人体离开） */
    int level = gpio_get_level(c->gpio_pin);
    dal_pir_edge_t edge = (level == 1) ? DAL_PIR_EDGE_RISING : DAL_PIR_EDGE_FALLING;
    c->cb(edge, c->user);
}

/* ================================================================
 *  dal_pir_ops_t 实现
 * ================================================================ */

/** @brief 读电平推断状态（轮询兜底） */
static dal_pir_state_t pir_read_state(bsp_pir_ctx_t *c)
{
    int level = gpio_get_level(c->gpio_pin);
    return (level == 1) ? DAL_PIR_STATE_MOTION : DAL_PIR_STATE_IDLE;
}

static dal_err_t pir_init(void *ctx_)
{
    bsp_pir_ctx_t *c = (bsp_pir_ctx_t *)ctx_;
    if (c == NULL) {
        return DAL_ERR_INVALID;
    }
    if (c->inited) {
        return DAL_ERR_STATE;
    }

    c->gpio_pin = (gpio_num_t)BOARD_PIR_INTR_PIN;

    /* 配置 GPIO 输入 + 下拉（HC-SR501 高电平有效，下拉保证空闲为 IDLE） */
    esp_err_t ret = gpio_set_direction(c->gpio_pin, GPIO_MODE_INPUT);
    if (ret != ESP_OK) {
        return dal_err_from_esp(ret);
    }
    gpio_set_pull_mode(c->gpio_pin, GPIO_PULLDOWN_ONLY);

    c->inited = true;
    ESP_LOGI(PIR_TAG, "initialized (gpio=%d, intr-ready)", c->gpio_pin);
    return DAL_OK;
}

static dal_err_t pir_get_state(void *ctx_, dal_pir_state_t *state)
{
    bsp_pir_ctx_t *c = (bsp_pir_ctx_t *)ctx_;
    if (c == NULL || !c->inited || state == NULL) {
        return DAL_ERR_INVALID;
    }
    *state = pir_read_state(c);
    return DAL_OK;
}

static dal_err_t pir_set_edge_cb(void *ctx_, dal_pir_edge_cb_t cb, void *user)
{
    bsp_pir_ctx_t *c = (bsp_pir_ctx_t *)ctx_;
    if (c == NULL || !c->inited) {
        return DAL_ERR_STATE;
    }

    /* 先禁中断再改回调，避免竞态 */
    if (c->intr_enabled) {
        gpio_intr_disable(c->gpio_pin);
        c->intr_enabled = false;
    }

    c->cb   = cb;
    c->user = user;

    if (cb == NULL) {
        /* NULL 表示禁用中断：注销 ISR */
        gpio_set_intr_type(c->gpio_pin, GPIO_INTR_DISABLE);
        gpio_isr_handler_remove(c->gpio_pin);
        return DAL_OK;
    }

    /* 双沿中断：上升沿=人体进入，下降沿=人体离开。
     * gpio_install_isr_service 全局只需一次，重复调用返回 ESP_ERR_INVALID_STATE（忽略）。 */
    esp_err_t ret = gpio_set_intr_type(c->gpio_pin, GPIO_INTR_ANYEDGE);
    if (ret != ESP_OK) {
        return dal_err_from_esp(ret);
    }
    esp_err_t svc = gpio_install_isr_service(0);
    if (svc != ESP_OK && svc != ESP_ERR_INVALID_STATE) {
        return dal_err_from_esp(svc);
    }
    ret = gpio_isr_handler_add(c->gpio_pin, pir_gpio_isr_handler, c);
    if (ret != ESP_OK) {
        return dal_err_from_esp(ret);
    }
    ret = gpio_intr_enable(c->gpio_pin);
    if (ret != ESP_OK) {
        gpio_isr_handler_remove(c->gpio_pin);
        return dal_err_from_esp(ret);
    }
    c->intr_enabled = true;
    return DAL_OK;
}

static dal_err_t pir_deinit(void *ctx_)
{
    bsp_pir_ctx_t *c = (bsp_pir_ctx_t *)ctx_;
    if (c == NULL || !c->inited) {
        return DAL_ERR_INVALID;
    }
    /* 禁中断 + 注销 ISR，再标记未初始化 */
    if (c->intr_enabled) {
        gpio_intr_disable(c->gpio_pin);
        gpio_isr_handler_remove(c->gpio_pin);
        c->intr_enabled = false;
    }
    c->cb = NULL;
    c->user = NULL;
    c->inited = false;
    return DAL_OK;
}

/* ================================================================
 *  静态 ops + ctx（单实例，ctx 编译期注入 ops->ctx）
 * ================================================================ */
static dal_pir_ops_t s_pir_ops = {
    .init        = pir_init,
    .get_state   = pir_get_state,
    .set_edge_cb = pir_set_edge_cb,
    .deinit      = pir_deinit,
    .ctx         = &s_ctx,
};

/* ================================================================
 *  对外 create 入口（仅返回 ops 指针，不注册、不初始化硬件）
 * ================================================================ */
dal_pir_ops_t *bsp_pir_hcsr501_create(void)
{
    /* 静态 ops 编译期已注入 ctx；此处仅做非硬件的 ctx 字段清零。
     * 单实例：上层调 ops->init(ops->ctx) 触发硬件初始化。 */
    memset(&s_ctx, 0, sizeof(s_ctx));
    return &s_pir_ops;
}
