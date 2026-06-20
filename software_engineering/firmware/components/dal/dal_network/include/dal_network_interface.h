/**
 * @file    dal_network_interface.h
 * @brief   网络设备 DAL 接口契约（纯业务语义，无硬件依赖）
 *
 * @details 此文件是网络模块的纯业务语义接口规范。
 *          - Service 层、Mock 测试、BSP 适配层均应仅包含此文件。
 *          - 严禁包含任何平台头文件（如 esp_eth.h、esp_netif.h）或
 *            DAL 管理头（dal_network.h）。
 *          - 业务语义仅含 IP 配置（DHCP/静态）与连接查询，
 *            MAC/PHY 引脚等硬件细节封装在 BSP 内部。
 *          - 所有硬件上下文封装在不透明指针 void *ctx 中。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef DAL_NETWORK_INTERFACE_H
#define DAL_NETWORK_INTERFACE_H

#include "dal_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 网络连接状态 */
typedef enum {
    DAL_NETWORK_DISCONNECTED = 0,   /**< 断开（链路 down 或无 IP） */
    DAL_NETWORK_CONNECTED    = 1,   /**< 已连接（链路 up 且 IP 就绪） */
} dal_network_state_t;

/** IP 配置（纯业务语义） */
typedef struct {
    bool use_dhcp;              /**< true=DHCP, false=静态 IP */
    char static_ip[16];         /**< 静态 IP（DHCP 时忽略） */
    char netmask[16];           /**< 子网掩码 */
    char gateway[16];           /**< 网关 */
} dal_network_config_t;

/**
 * @brief 连接状态变化回调（BSP 事件处理任务调用，非 ISR）
 *
 * @param[in] user_data init 注册时传入的用户数据
 * @param[in] state     新的连接状态
 */
typedef void (*dal_network_state_cb_t)(void *user_data, dal_network_state_t state);

/** 网络操作契约 */
typedef struct {
    /**
     * @brief 初始化以太网（MAC + PHY + netif）并启动，注册状态回调
     * @param[in] ctx       驱动上下文
     * @param[in] cfg       IP 配置
     * @param[in] on_state  连接状态变化回调（可为 NULL）
     * @param[in] user_data 传给回调的用户数据
     * @return DAL_OK 成功
     */
    dal_err_t (*init)(void *ctx, const dal_network_config_t *cfg,
                      dal_network_state_cb_t on_state, void *user_data);

    /**
     * @brief 获取 IPv4 地址字符串
     * @param[in]  ctx    驱动上下文
     * @param[out] ip     输出缓冲（至少 16 字节）
     * @param[in]  ip_len 缓冲长度
     * @return DAL_OK 成功，DAL_ERR_STATE 未获取到 IP
     */
    dal_err_t (*get_ip)(void *ctx, char *ip, size_t ip_len);

    /**
     * @brief 查询是否已连接（链路 up + IP 就绪）
     */
    bool (*is_connected)(void *ctx);

    /** @brief 反初始化 */
    dal_err_t (*deinit)(void *ctx);
} dal_network_ops_t;

#ifdef __cplusplus
}
#endif
#endif /* DAL_NETWORK_INTERFACE_H */
