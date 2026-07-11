/**
 * @file    bsp_pir_hcsr501.h
 * @brief   HC-SR501 人体红外感应 BSP — create + ctx 绑定入口
 *
 * @details 仅负责创建 ops + ctx 绑定并返回 ops 指针，不调用 DAL 注册 API，
 *          不驱动硬件。注册由板级组装器（board_v1_init）通过
 *          dal_pir_register(name, ops, ops->ctx) 完成；硬件初始化由上层通过
 *          ops->init(ops->ctx) 按需触发。PIR 引脚号仅在本 BSP .c 内可见，
 *          Service 层完全盲化。
 *
 * @note    此文件只声明 create 入口：
 *          - 不包含 GPIO 引脚定义
 *          - 不包含 dal_pir_ops_t 或硬件上下文结构体
 *          - Service 层不应包含此文件
 *
 * @author  xLumina
 * @version 1.2
 */
#ifndef BSP_PIR_HCSR501_H
#define BSP_PIR_HCSR501_H

#include "dal_err.h"
#include "dal_pir_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 HC-SR501 PIR 的 ops + ctx 绑定
 *
 * @return 指向静态 ops（ops->ctx 已注入静态 ctx）
 *
 * @note 仅做 struct 绑定（静态 ops 编译期已注入 ctx），零硬件副作用。
 *       硬件初始化（GPIO 输入+下拉）由上层调 ops->init(ops->ctx) 触发。
 *       轮询式：上层周期调 ops->get_state(ops->ctx, &state) 读运动状态，
 *       去抖/状态变化通知由上层处理。BSP 不创建任务、不注册中断。
 *       注册由板级组装器调用 dal_pir_register(name, ops, ops->ctx) 完成。
 *       PIR 引脚取自 board_v1_config.h 的 BOARD_PIR_INTR_PIN，仅在 .c 内可见。
 */
dal_pir_ops_t *bsp_pir_hcsr501_create(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_PIR_HCSR501_H */
