/**
 * @file    dal_pir_interface.h
 * @brief   人体红外感应设备 DAL 接口契约（纯业务语义，无硬件依赖）
 *
 * @details 此文件是 PIR 模块的纯业务语义接口规范。
 *          - Service 层、Mock 测试、BSP 适配层均应仅包含此文件。
 *          - 严禁包含任何平台头文件（如 pal_gpio.h）或 DAL 管理头（dal_pir.h）。
 *          - 事件模型：BSP init 注册 GPIO 中断，ISR 投递信号量唤醒内部
 *            任务，任务读电平推断运动状态后经 on_state 回调投递给 Service
 *            （回调运行在 BSP 任务上下文，非 ISR，可执行较复杂逻辑）。
 *          - 所有硬件上下文封装在不透明指针 void *ctx 中。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef DAL_PIR_INTERFACE_H
#define DAL_PIR_INTERFACE_H

#include "dal_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** PIR 运动状态 */
typedef enum {
    DAL_PIR_STATE_IDLE   = 0,   /**< 空闲（无人体感应） */
    DAL_PIR_STATE_MOTION = 1,   /**< 检测到人体运动 */
} dal_pir_state_t;

/**
 * @brief 运动状态变化回调（BSP 内部任务调用，非 ISR）
 *
 * @param[in] user_data init 注册时传入的用户数据
 * @param[in] state     新的运动状态
 */
typedef void (*dal_pir_state_cb_t)(void *user_data, dal_pir_state_t state);

/** PIR 操作契约 */
typedef struct {
    /**
     * @brief 初始化 PIR 并注册状态变化回调
     * @param[in] ctx       驱动上下文
     * @param[in] on_state  状态变化回调（不可为 NULL）
     * @param[in] user_data 传给回调的用户数据
     * @return DAL_OK 成功
     */
    dal_err_t (*init)(void *ctx, dal_pir_state_cb_t on_state, void *user_data);

    /**
     * @brief 使能 PIR 中断检测
     * @return DAL_OK 成功
     */
    dal_err_t (*enable)(void *ctx);

    /**
     * @brief 禁用 PIR 中断检测
     * @return DAL_OK 成功
     */
    dal_err_t (*disable)(void *ctx);

    /**
     * @brief 同步读取当前运动状态
     * @param[out] state 输出状态
     * @return DAL_OK 成功
     */
    dal_err_t (*get_state)(void *ctx, dal_pir_state_t *state);

    /** @brief 反初始化 */
    dal_err_t (*deinit)(void *ctx);
} dal_pir_ops_t;

#ifdef __cplusplus
}
#endif
#endif /* DAL_PIR_INTERFACE_H */
