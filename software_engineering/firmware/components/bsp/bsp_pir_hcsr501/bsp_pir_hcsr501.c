/**
 * @file    bsp_pir_hcsr501.c
 * @brief   HC-SR501 PIR BSP 实现 — GPIO 中断 + 任务回调，自注册到 DAL
 *
 * @details HC-SR501 输出高电平表示检测到人体运动。本驱动：
 *          - 配置 GPIO 输入 + 下拉，注册 ANYEDGE 中断。
 *          - ISR（IRAM_ATTR）仅用 xSemaphoreGiveFromISR 投递二值信号量，
 *            符合「ISR 简洁」规范（osal 无 FromISR 接口，故 BSP 直接用
 *            FreeRTOS API，与 network 直接用 esp_eth 同理）。
 *          - 内部常驻任务被信号量唤醒，读 GPIO 电平推断运动状态，
 *            状态变化时调 Service 注册的回调（任务上下文，非 ISR）。
 *          PAL GPIO 返回码经 dal_err_from_pal 翻译为 dal_err_t。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "bsp_pir_hcsr501.h"
#include "dal_pir.h"
#include "dal_pir_interface.h"
#include "dal_pal_err.h"
#include "bsp_config.h"
#include "pal_gpio.h"
#include "pal_log.h"
#include "osal_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* ---- 任务参数 ---- */
#define PIR_TASK_STACK     2048
#define PIR_TASK_PRIORITY  5
#define PIR_TASK_CORE      (-1)
/** 任务空闲等待超时（ms），避免 portMAX_DELAY 经 pdMS_TO_TICKS 溢出 */
#define PIR_IDLE_WAIT_MS   30000u

/* ---- BSP 私有上下文 ---- */
typedef struct {
    int                   gpio_pin;
    bool                  inited;
    bool                  enabled;
    dal_pir_state_t       last_state;   /**< 上次通知的状态（去抖） */
    dal_pir_state_cb_t    on_state;     /**< Service 回调 */
    void                 *user_data;
    SemaphoreHandle_t     isr_sem;      /**< ISR → 任务 唤醒信号量 */
    osal_task_handle_t    task;         /**< 内部任务句柄 */
} bsp_pir_ctx_t;

static bsp_pir_ctx_t s_ctx;

/* ---- ISR：仅投信号量（IRAM_ATTR）---- */
static void IRAM_ATTR pir_isr_cb(void *arg)
{
    bsp_pir_ctx_t *c = (bsp_pir_ctx_t *)arg;
    BaseType_t higher_task_woken = pdFALSE;
    if (c != NULL && c->isr_sem != NULL) {
        xSemaphoreGiveFromISR(c->isr_sem, &higher_task_woken);
    }
    if (higher_task_woken) {
        portYIELD_FROM_ISR();
    }
}

/* ---- 读电平推断状态 ---- */
static dal_pir_state_t pir_read_state(bsp_pir_ctx_t *c)
{
    int level = pal_gpio_read(c->gpio_pin);
    return (level == 1) ? DAL_PIR_STATE_MOTION : DAL_PIR_STATE_IDLE;
}

/* ---- 内部任务：信号量唤醒 → 读状态 → 回调 ---- */
static void pir_task(void *arg)
{
    bsp_pir_ctx_t *c = (bsp_pir_ctx_t *)arg;

    while (1) {
        /* 等待 ISR 唤醒；超时则周期性重检（应对丢中断） */
        if (xSemaphoreTake(c->isr_sem, pdMS_TO_TICKS(PIR_IDLE_WAIT_MS)) == pdFALSE) {
            continue;
        }

        if (!c->enabled || c->on_state == NULL) {
            continue;
        }

        dal_pir_state_t st = pir_read_state(c);
        if (st != c->last_state) {
            c->last_state = st;
            c->on_state(c->user_data, st);
        }
    }
}

/* ================================================================
 *  dal_pir_ops_t 实现
 * ================================================================ */
static dal_err_t pir_init(void *ctx_, dal_pir_state_cb_t on_state, void *user_data)
{
    bsp_pir_ctx_t *c = (bsp_pir_ctx_t *)ctx_;
    if (c == NULL || on_state == NULL) {
        return DAL_ERR_INVALID;
    }

    c->gpio_pin  = BOARD_PIR_INTR_PIN;
    c->on_state  = on_state;
    c->user_data = user_data;
    c->enabled   = false;
    c->last_state = DAL_PIR_STATE_IDLE;

    /* 配置 GPIO 输入 + 下拉 */
    int ret = pal_gpio_set_direction(c->gpio_pin, PAL_GPIO_DIR_INPUT);
    if (ret != 0) {
        return dal_err_from_pal(ret);
    }
    pal_gpio_set_pull_mode(c->gpio_pin, PAL_GPIO_PULL_DOWN);

    /* 安装 GPIO ISR 服务（幂等） */
    ret = pal_gpio_install_isr_service(0);
    if (ret != 0) {
        PAL_LOGW("PIR", "isr service install ret=%d (may already installed)", ret);
    }

    /* 创建 ISR → 任务 信号量（二值） */
    c->isr_sem = xSemaphoreCreateBinary();
    if (c->isr_sem == NULL) {
        return DAL_ERR_NO_MEM;
    }

    /* 注册 ANYEDGE 中断 */
    ret = pal_gpio_set_intr(c->gpio_pin, PAL_GPIO_INTR_ANYEDGE, pir_isr_cb, c);
    if (ret != 0) {
        vSemaphoreDelete(c->isr_sem);
        c->isr_sem = NULL;
        return dal_err_from_pal(ret);
    }

    /* 创建内部任务（暂不使能中断，等 enable()） */
    c->task = osal_task_create("bsp_pir", pir_task, c,
                               PIR_TASK_STACK, PIR_TASK_PRIORITY, PIR_TASK_CORE);
    if (c->task == NULL) {
        pal_gpio_remove_intr(c->gpio_pin);
        vSemaphoreDelete(c->isr_sem);
        c->isr_sem = NULL;
        return DAL_ERR_NO_MEM;
    }

    c->inited = true;
    PAL_LOGI("PIR", "initialized (gpio=%d)", c->gpio_pin);
    return DAL_OK;
}

static dal_err_t pir_enable(void *ctx_)
{
    bsp_pir_ctx_t *c = (bsp_pir_ctx_t *)ctx_;
    if (c == NULL || !c->inited) {
        return DAL_ERR_INVALID;
    }
    if (c->enabled) {
        return DAL_OK;
    }
    int ret = pal_gpio_intr_enable(c->gpio_pin);
    if (ret != 0) {
        return dal_err_from_pal(ret);
    }
    c->enabled = true;
    return DAL_OK;
}

static dal_err_t pir_disable(void *ctx_)
{
    bsp_pir_ctx_t *c = (bsp_pir_ctx_t *)ctx_;
    if (c == NULL || !c->inited) {
        return DAL_ERR_INVALID;
    }
    if (!c->enabled) {
        return DAL_OK;
    }
    int ret = pal_gpio_intr_disable(c->gpio_pin);
    if (ret != 0) {
        return dal_err_from_pal(ret);
    }
    c->enabled = false;
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

static dal_err_t pir_deinit(void *ctx_)
{
    bsp_pir_ctx_t *c = (bsp_pir_ctx_t *)ctx_;
    if (c == NULL || !c->inited) {
        return DAL_ERR_INVALID;
    }
    pir_disable(c);
    pal_gpio_remove_intr(c->gpio_pin);
    if (c->isr_sem != NULL) {
        vSemaphoreDelete(c->isr_sem);
        c->isr_sem = NULL;
    }
    /* 任务常驻不删除（osal 无安全删除机制）；标记未初始化 */
    c->inited = false;
    return DAL_OK;
}

static const dal_pir_ops_t s_pir_ops = {
    .init      = pir_init,
    .enable    = pir_enable,
    .disable   = pir_disable,
    .get_state = pir_get_state,
    .deinit    = pir_deinit,
};

/* ================================================================
 *  对外初始化入口（自注册）
 * ================================================================ */

/* 内部占位回调：bsp_pir_hcsr501_init 用它完成硬件初始化使设备就绪。
 * Service 取得 ops 后，若需自定义回调，可 deinit + init(callback) 重设。 */
static void pir_noop_cb(void *user_data, dal_pir_state_t state)
{
    (void)user_data;
    (void)state;
}

dal_err_t bsp_pir_hcsr501_init(void)
{
    /* 用占位回调完成硬件初始化（GPIO/中断/任务就绪），暂不 enable。
     * Service 通过 dal_pir_get("main_pir", ...) 取 ops 后调 enable() 开始检测。
     * 若需自定义状态回调，Service 可 deinit + init(自定义回调) 重设。 */
    dal_err_t ret = pir_init(&s_ctx, pir_noop_cb, NULL);
    if (ret != DAL_OK) {
        return ret;
    }

    return dal_pir_register("main_pir", &s_pir_ops, &s_ctx);
}

