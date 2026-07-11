/**
 * @file    dal_pir_interface.h
 * @brief   人体红外感应设备 DAL 接口契约（纯业务语义，无硬件依赖）
 *
 * @details 此文件是 PIR 模块的纯业务语义接口规范。
 *          - Service 层、Mock 测试、BSP 适配层均应仅包含此文件。
 *          - 严禁包含任何平台头文件或 DAL 管理头（dal_pir.h）。
 *          - 中断模型：BSP 在 set_edge_cb 中安装 GPIO 双沿中断，ISR 仅调用上层
 *            注册的边沿回调（cb 在 ISR 上下文）。回调内由上层做 xTaskNotifyFromISR
 *            等信号操作；去抖/离开确认定时器在任务上下文完成。BSP/DAL 不维护 OS 任务。
 *          - get_state 保留为轮询兜底（自检/调试），正常采集走中断回调。
 *          - 所有硬件上下文封装在不透明指针 void *ctx 中。
 *
 * @author  xiamu
 * @version 1.2
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

/** PIR 边沿类型（ISR 回调用） */
typedef enum {
    DAL_PIR_EDGE_RISING  = 0,   /**< 上升沿（人体进入） */
    DAL_PIR_EDGE_FALLING = 1,   /**< 下降沿（人体离开） */
} dal_pir_edge_t;

/**
 * @brief PIR 边沿中断回调原型
 *
 * @param[in] edge 触发的边沿类型
 * @param[in] user 注册时传入的用户数据
 *
 * @note 本回调在 **ISR 上下文** 执行，遵循中断铁律：仅做信号量/任务通知类操作，
 *       禁止日志、内存操作、去抖延时。去抖与状态机由任务上下文完成。
 */
typedef void (*dal_pir_edge_cb_t)(dal_pir_edge_t edge, void *user);

/** PIR 操作契约 */
typedef struct {
    /**
     * @brief 初始化 PIR（配置 GPIO 输入）
     * @param[in] ctx 驱动上下文
     * @return DAL_OK 成功
     *
     * @note 配置 GPIO 输入引脚 + 下拉，不创建任务。中断需由 set_edge_cb 显式启用。
     */
    dal_err_t (*init)(void *ctx);

    /**
     * @brief 同步读取当前运动状态（轮询兜底，可用于自检/调试）
     * @param[out] state 输出状态
     * @return DAL_OK 成功
     */
    dal_err_t (*get_state)(void *ctx, dal_pir_state_t *state);

    /**
     * @brief 注册边沿回调并启用 GPIO 双沿中断
     *
     * @param[in] ctx   驱动上下文
     * @param[in] cb    边沿回调（ISR 上下文），传 NULL 表示禁用中断
     * @param[in] user  回调用户数据
     * @return DAL_OK 成功，DAL_ERR_STATE 未 init
     *
     * @note ISR 仅调用 cb；cb 内由上层做 xTaskNotifyFromISR 等信号操作。
     *       去抖/离开确认定时器在任务上下文实现，符合手册中断铁律。
     */
    dal_err_t (*set_edge_cb)(void *ctx, dal_pir_edge_cb_t cb, void *user);

    /** @brief 反初始化 */
    dal_err_t (*deinit)(void *ctx);

    void *ctx;              /**< BSP 私有上下文，由 create() 注入 */
} dal_pir_ops_t;

#ifdef __cplusplus
}
#endif
#endif /* DAL_PIR_INTERFACE_H */
