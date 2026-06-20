/**
 * @file    bsp_pir_hcsr501.h
 * @brief   HC-SR501 人体红外感应 BSP - 公共接口
 *
 * @note    此文件仅声明本板 PIR 子系统的初始化入口。
 *          - 不包含 GPIO 引脚定义
 *          - 不包含 dal_pir_ops_t 或硬件上下文结构体
 *          - Service 层不应包含此文件
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef BSP_PIR_HCSR501_H
#define BSP_PIR_HCSR501_H

#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 HC-SR501 PIR 并自注册到 DAL
 *
 * @details 完成：
 *          1. 配置 PIR GPIO 为输入（带下拉），安装 GPIO ISR 服务
 *          2. 注册 ANYEDGE 中断，创建内部任务（ISR 投信号量 → 任务调回调）
 *          3. 以名称 "main_pir" 自注册到 DAL pir 模块
 *
 * @return DAL_OK 成功
 * @retval DAL_ERR_HW     底层硬件错误
 * @retval DAL_ERR_STATE  DAL 注册冲突
 * @retval DAL_ERR_NO_MEM 资源不足
 *
 * @note PIR 引脚取自 bsp_config.h 的 BOARD_PIR_INTR_PIN。
 *       ⚠️ 若该引脚与其它设备冲突需调整 bsp_config。
 *       init 时不自动 enable，Service 需调 enable() 开始检测。
 */
dal_err_t bsp_pir_hcsr501_init(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_PIR_HCSR501_H */
