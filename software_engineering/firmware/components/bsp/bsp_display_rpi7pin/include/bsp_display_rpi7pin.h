/**
 * @file    bsp_display_rpi7pin.h
 * @brief   RPI 7 寸屏 BSP 聚合层 — create + ctx 绑定入口
 *
 * @details 聚合 TC358762（DSI 桥 + 显示）与 ATTINY88（背光/电源）两颗子芯片，
 *          实现 dal_display_ops_t 契约。仅负责创建 ops + ctx 绑定并返回，
 *          不调用 DAL 注册 API，不驱动硬件。注册由板级组装器（board_v1）通过
 *          dal_display_register() 完成；硬件初始化由上层通过 ops->init(ctx, cfg)
 *          触发（编排两阶段上电序列）。引脚/时序/I2C 地址仅在本 BSP .c 内可见，
 *          Service 层完全盲化。
 *
 *          - 不包含引脚定义、时序、I2C 地址等硬件细节
 *          - 不包含硬件上下文结构体
 *          - Service 层不应包含此文件
 *
 * @author  xLumina
 * @version 2.0
 */
#ifndef BSP_DISPLAY_RPI7PIN_H
#define BSP_DISPLAY_RPI7PIN_H

#include "dal_err.h"
#include "dal_display_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 RPI 7 寸屏显示设备的 ops（ctx 编译期已注入）
 *
 * @return 非 NULL：指向静态 ops（ops->ctx 已注入对应聚合层静态 ctx）
 *
 * @note 仅做 struct 字段初始化（memset ctx），零硬件副作用。
 *       硬件初始化由上层调 ops->init(ctx, cfg) 触发，编排两阶段上电序列：
 *         (1) ATTINY88 init（共享 I2C）
 *         (2) TC358762 init（DSI bus + DPI panel + 帧缓冲 + LDO）
 *         (3) ATTINY88 power_on
 *         (4) ATTINY88 release_reset
 *         (5) TC358762 config_bridge（DSI Generic Write 14 寄存器 + 启动 PPI/DSI）
 *         (6) ATTINY88 set_backlight（初始亮度）
 *       注册由板级组装器调用 dal_display_register(name, ops, ops->ctx) 完成。
 *       依赖共享 I2C 总线已由 board_v1 初始化。
 */
dal_display_ops_t *bsp_display_rpi7pin_create(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_DISPLAY_RPI7PIN_H */
