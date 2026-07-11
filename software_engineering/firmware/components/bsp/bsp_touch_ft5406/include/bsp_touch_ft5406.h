/**
 * @file    bsp_touch_ft5406.h
 * @brief   FT5406 触摸屏 BSP — create + ctx 绑定入口
 *
 * @details 仅负责创建 ops + ctx 绑定并返回，不调用 DAL 注册 API，
 *          不驱动硬件。注册由板级组装器（board_v1_init）通过
 *          dal_touch_register() 完成；硬件初始化（I2C 设备挂载 + 触摸
 *          控制器寄存器配置）由上层通过 ops->init() 按需触发。I2C 地址、
 *          引脚细节仅在本 BSP .c 内可见，Service 层完全盲化。
 *
 *          本板单实例（main_touch），ops->ctx 编译期注入。
 *
 * @author  xiamu
 * @version 1.1
 */
#ifndef BSP_TOUCH_FT5406_H
#define BSP_TOUCH_FT5406_H

#include "dal_err.h"
#include "dal_touch_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 FT5406 触摸 ops（ctx 编译期已注入）
 *
 * @return 非 NULL：指向静态 ops（ops->ctx 已注入对应静态 ctx）
 *
 * @note 仅做 struct 字段初始化（memset ctx），零硬件副作用。硬件初始化
 *       （共享 I2C 总线挂载 FT5406 设备 + 写寄存器序列）由上层调
 *       ops->init(ctx, cfg) 触发。
 *       注册由板级组装器调用 dal_touch_register("main_touch", ops, ops->ctx) 完成。
 */
dal_touch_ops_t *bsp_touch_ft5406_create(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_TOUCH_FT5406_H */
