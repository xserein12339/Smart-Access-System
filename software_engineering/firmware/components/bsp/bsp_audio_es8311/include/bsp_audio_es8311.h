/**
 * @file    bsp_audio_es8311.h
 * @brief   ES8311 音频 BSP — create + ctx 绑定入口
 *
 * @details 仅负责创建 ops + ctx 绑定并返回 ops 指针，不调用 DAL 注册 API，
 *          不驱动硬件。注册由板级组装器（board_v1_init）通过
 *          dal_audio_register(name, ops, ops->ctx) 完成；硬件初始化（ES8311
 *          codec 寄存器配置 + I2S 通道 + PA 使能）由上层通过
 *          ops->init(ops->ctx) 按需触发。I2C/I2S 引脚、地址细节仅在本
 *          BSP .c 内可见，Service 层完全盲化。
 *
 *          本板单实例（main_audio），ctx 编译期注入静态 ops->ctx。
 *
 * @author  xiamu
 * @version 1.2
 */
#ifndef BSP_AUDIO_ES8311_H
#define BSP_AUDIO_ES8311_H

#include "dal_err.h"
#include "dal_audio_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 ES8311 音频 ops + ctx 绑定
 *
 * @return 指向静态 ops（ops->ctx 已注入静态 ctx）
 *
 * @note 仅做 struct 绑定（静态 ops 编译期已注入 ctx），零硬件副作用。
 *       硬件初始化（共享 I2C 总线初始化 ES8311 codec + I2S STD 全双工通道
 *       + PA 使能）由上层调 ops->init(ops->ctx, cfg) 触发。
 *       注册由板级组装器调用 dal_audio_register(name, ops, ops->ctx) 完成。
 */
dal_audio_ops_t *bsp_audio_es8311_create(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_AUDIO_ES8311_H */
