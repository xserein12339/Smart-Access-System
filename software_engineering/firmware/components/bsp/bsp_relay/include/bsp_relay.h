/**
 * @file    bsp_relay.h
 * @brief   Board V1 继电器 BSP — create 入口（返回 ops + ctx 绑定）
 *
 * @details 仅负责创建 ops + ctx 绑定并返回 ops 指针，不调用 DAL 注册 API，
 *          不驱动硬件。注册由板级组装器（board_v1_init）通过
 *          dal_relay_register(name, ops, ops->ctx) 完成；硬件初始化由上层通过
 *          ops->init(ops->ctx) 按需触发。引脚号仅在本 BSP .c 内可见，
 *          Service 层完全盲化。
 *
 *          本板有 3 个继电器实例（door_lock / alarm / wiegand_pwr），
 *          各有独立静态 ops + ctx，通过 instance 名选择。
 *
 * @author  xiamu
 * @version 1.2
 */
#ifndef BSP_RELAY_H
#define BSP_RELAY_H

#include "dal_err.h"
#include "dal_relay_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建指定继电器实例的 ops + ctx 绑定
 *
 * @param[in] instance 实例名："door_lock" / "alarm" / "wiegand_pwr"
 *
 * @return 非 NULL：指向该实例静态 ops（ops->ctx 已注入对应静态 ctx）；
 *         NULL：未知实例名
 *
 * @note 仅做 struct 绑定（静态 ops 编译期已注入 ctx），零硬件副作用。
 *       硬件初始化由上层调 ops->init(ops->ctx) 触发。
 *       注册由板级组装器调用 dal_relay_register(name, ops, ops->ctx) 完成。
 */
dal_relay_ops_t *bsp_relay_create(const char *instance);

#ifdef __cplusplus
}
#endif
#endif /* BSP_RELAY_H */
