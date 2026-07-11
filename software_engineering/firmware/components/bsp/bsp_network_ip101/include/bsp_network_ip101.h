/**
 * @file    bsp_network_ip101.h
 * @brief   IP101 PHY 以太网 BSP — create + ctx 绑定入口
 *
 * @details 仅负责创建 ops + ctx 绑定并返回 ops 指针，不调用 DAL 注册 API，
 *          不驱动硬件。注册由板级组装器（board_v1_init）通过
 *          dal_network_register(name, ops, ops->ctx) 完成；硬件初始化
 *          （MAC+PHY+netif+DHCP/静态+事件+启动）由上层通过
 *          ops->init(ops->ctx) 按需触发。
 *
 * @note    此文件只声明 create 入口：
 *          - 不包含 MDC/MDIO 引脚、PHY 地址等硬件细节
 *          - 不包含 dal_network_ops_t 或硬件上下文结构体
 *          - Service 层不应包含此文件
 *
 * @author  xLumina
 * @version 1.2
 */
#ifndef BSP_NETWORK_IP101_H
#define BSP_NETWORK_IP101_H

#include "dal_err.h"
#include "dal_network_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 IP101 以太网的 ops + ctx 绑定
 *
 * @return 指向静态 ops（ops->ctx 已注入静态 ctx）
 *
 * @note 仅做 struct 绑定（静态 ops 编译期已注入 ctx），零硬件副作用。
 *       硬件初始化（esp_netif_init + 默认事件循环、EMAC + IP101 PHY、
 *       esp_eth 驱动安装、netif 绑定、事件注册、DHCP/静态 IP、esp_eth_start）
 *       由上层调 ops->init(ops->ctx, cfg, cb, user_data) 触发。注册由板级
 *       组装器调用 dal_network_register(name, ops, ops->ctx) 完成。启动后
 *       链路 Link Up + DHCP 获取 IP 异步完成，Service 可注册状态回调或轮询
 *       is_connected/get_ip。
 */
dal_network_ops_t *bsp_network_ip101_create(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_NETWORK_IP101_H */
