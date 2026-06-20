/**
 * @file    bsp_touch_ft5406.h
 * @brief   FT5406 触摸屏 BSP - 公共接口
 *
 * @note    此文件仅声明本板触摸子系统的初始化入口。
 *          - 不包含引脚定义、I2C 地址、寄存器细节
 *          - 不包含 dal_touch_ops_t 或硬件上下文结构体
 *          - Service 层不应包含此文件
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef BSP_TOUCH_FT5406_H
#define BSP_TOUCH_FT5406_H

#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 FT5406 触摸屏并自注册到 DAL
 *
 * @details 完成：
 *          1. 从 board_v1 获取共享 I2C 总线，挂载 FT5406 设备
 *          2. 写初始化寄存器序列（DEVICE_MODE / 阈值 / 活跃周期）
 *          3. 以名称 "main_touch" 自注册到 DAL touch 模块
 *
 * @return DAL_OK 成功
 * @retval DAL_ERR_HW     底层硬件错误
 * @retval DAL_ERR_STATE  共享 I2C 未就绪 / DAL 注册冲突
 *
 * @note 依赖共享 I2C 总线已由 board_v1 初始化。轮询模型，无中断引脚。
 *       Service 通过 dal_touch_get("main_touch", ...) 获取 ops，按固定间隔 read()。
 */
dal_err_t bsp_touch_ft5406_init(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_TOUCH_FT5406_H */
