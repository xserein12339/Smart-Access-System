/**
 * @file    bsp_relay.h
 * @brief   Board V1 继电器板级支持包 - 公共接口
 *
 * @note    此文件仅声明本板继电器子系统的初始化入口。
 *          - 不包含任何 GPIO 引脚定义
 *          - 不包含 dal_relay_ops_t 或硬件上下文结构体
 *          - Service 层不应包含此文件（Service 应直接使用 dal_relay_interface.h）
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */
#ifndef BSP_RELAY_H
#define BSP_RELAY_H

#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化本板所有继电器硬件并自注册到 DAL
 *
 * @details 完成以下工作：
 *          1. 配置所有继电器 GPIO 为输出模式并置于安全默认状态（断开）
 *          2. 将每个继电器以语义化名称注册到 DAL relay 模块
 *
 * @return DAL_OK 成功
 * @retval DAL_ERR_HW     GPIO 初始化失败
 * @retval DAL_ERR_STATE  DAL 注册名称冲突
 * @retval DAL_ERR_NO_MEM DAL 注册表满 / 互斥锁创建失败
 *
 * @note 必须在系统启动早期调用，且仅调用一次。
 *       调用完成后，Service 层可通过 dal_relay_get("door_lock", ...)
 *       访问继电器，无需关心底层硬件。
 */
dal_err_t bsp_relay_init(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_RELAY_H */
