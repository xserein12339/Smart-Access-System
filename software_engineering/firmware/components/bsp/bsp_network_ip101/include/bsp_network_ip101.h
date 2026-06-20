/**
 * @file    bsp_network_ip101.h
 * @brief   IP101 PHY 以太网 BSP - 公共接口
 *
 * @note    此文件仅声明本板以太网子系统的初始化入口。
 *          - 不包含 MDC/MDIO 引脚、PHY 地址等硬件细节
 *          - 不包含 dal_network_ops_t 或硬件上下文结构体
 *          - Service 层不应包含此文件
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef BSP_NETWORK_IP101_H
#define BSP_NETWORK_IP101_H

#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 IP101 以太网并自注册到 DAL
 *
 * @details 完成：
 *          1. esp_netif_init + 默认事件循环
 *          2. 创建 EMAC + IP101 PHY（参数取自 bsp_config.h）
 *          3. 安装 esp_eth 驱动，绑定 netif，注册事件
 *          4. DHCP（默认）或静态 IP
 *          5. esp_eth_start，以名称 "main_eth" 自注册到 DAL
 *
 * @return DAL_OK 成功
 * @retval DAL_ERR_HW 底层初始化失败
 * @retval DAL_ERR_STATE DAL 注册冲突
 *
 * @note 启动后链路 Link Up + DHCP 获取 IP 异步完成，Service 可注册
 *       状态回调或轮询 is_connected/get_ip。
 */
dal_err_t bsp_network_ip101_init(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_NETWORK_IP101_H */
