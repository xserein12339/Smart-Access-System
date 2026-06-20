/**
 * @file    bsp_display_rpi7pin.h
 * @brief   RPI 7 寸屏 BSP 组装器 - 公共接口
 *
 * @details 此文件仅声明本板显示子系统的初始化入口。
 *          - 不包含引脚定义、时序、I2C 地址等硬件细节
 *          - 不包含 dal_display_ops_t 或硬件上下文结构体
 *          - Service 层不应包含此文件
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef BSP_DISPLAY_RPI7PIN_H
#define BSP_DISPLAY_RPI7PIN_H

#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 RPI 7 寸屏（TC358762 + ATTINY88）并自注册到 DAL
 *
 * @details 完成：
 *          1. 初始化 TC358762 DSI 桥接（参数取自 bsp_config.h）
 *          2. 在共享 I2C 总线上挂载 ATTINY88 背光 MCU
 *          3. 以名称 "rpi7pin" 自注册到 DAL display 模块
 *
 * @return DAL_OK 成功
 * @retval DAL_ERR_HW 底层硬件错误
 * @retval DAL_ERR_STATE DAL 注册名称冲突
 *
 * @note 依赖共享 I2C 总线已由 board_v1 初始化。系统启动早期调用，仅一次。
 */
dal_err_t bsp_display_rpi7pin_init(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_DISPLAY_RPI7PIN_H */
