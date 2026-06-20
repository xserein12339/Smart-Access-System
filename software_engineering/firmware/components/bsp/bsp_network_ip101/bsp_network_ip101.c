/**
 * @file    bsp_network_ip101.c
 * @brief   IP101 PHY 以太网 BSP 实现 — esp_eth + esp_netif，自注册到 DAL
 *
 * @details 移植参考工程的 esp_eth 初始化序列，适配当前工程的 BSP 私有 +
 *          dal_err_t + 状态回调模型。无 PAL 网络封装层，BSP 直接用 esp_eth。
 *          esp_err_t 经 dal_err_from_pal 翻译为 dal_err_t。
 *
 *          状态回调：IP_EVENT_ETH_GOT_IP 时通知 CONNECTED，
 *          ETHERNET_EVENT_DISCONNECTED 时通知 DISCONNECTED。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "bsp_network_ip101.h"
#include "dal_network.h"
#include "dal_network_interface.h"
#include "dal_pal_err.h"
#include "bsp_config.h"
#include "pal_log.h"

#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_err.h"

#include <string.h>
#include <stdio.h>

/* ---- BSP 私有上下文 ---- */
typedef struct {
    esp_eth_handle_t             eth_handle;
    esp_eth_mac_t               *mac;
    esp_eth_phy_t               *phy;
    esp_eth_netif_glue_handle_t  glue;
    esp_netif_t                 *netif;
    bool                         inited;
    bool                         started;
    bool                         connected;
    bool                         eth_event_registered;
    bool                         ip_event_registered;
    /* 状态回调（init 时由 DAL ops 设置） */
    dal_network_state_cb_t       on_state;
    void                        *user_data;
} bsp_network_ctx_t;

static bsp_network_ctx_t s_ctx;

/* ---- esp_eth 事件处理（默认事件循环任务上下文，非 ISR）---- */
static void eth_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    (void)base;
    (void)data;
    bsp_network_ctx_t *c = (bsp_network_ctx_t *)arg;
    if (c == NULL) return;

    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        PAL_LOGI("NET", "link up");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        PAL_LOGI("NET", "link down");
        if (c->connected) {
            c->connected = false;
            if (c->on_state) {
                c->on_state(c->user_data, DAL_NETWORK_DISCONNECTED);
            }
        }
        break;
    case ETHERNET_EVENT_START:
        PAL_LOGI("NET", "started");
        break;
    case ETHERNET_EVENT_STOP:
        PAL_LOGI("NET", "stopped");
        break;
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                             int32_t id, void *data)
{
    (void)base;
    bsp_network_ctx_t *c = (bsp_network_ctx_t *)arg;
    if (c == NULL) return;

    if (id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        PAL_LOGI("NET", "got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        if (!c->connected) {
            c->connected = true;
            if (c->on_state) {
                c->on_state(c->user_data, DAL_NETWORK_CONNECTED);
            }
        }
    }
}

/* ---- 资源释放（逆序）---- */
static void net_cleanup(bsp_network_ctx_t *c)
{
    if (c == NULL) return;
    if (c->ip_event_registered) {
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, ip_event_handler);
        c->ip_event_registered = false;
    }
    if (c->eth_event_registered) {
        esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler);
        c->eth_event_registered = false;
    }
    if (c->started && c->eth_handle) {
        esp_eth_stop(c->eth_handle);
        c->started = false;
    }
    if (c->glue) {
        esp_eth_del_netif_glue(c->glue);
        c->glue = NULL;
    }
    if (c->eth_handle) {
        esp_eth_driver_uninstall(c->eth_handle);
        c->eth_handle = NULL;
    }
    if (c->phy) {
        c->phy->del(c->phy);
        c->phy = NULL;
    }
    if (c->mac) {
        c->mac->del(c->mac);
        c->mac = NULL;
    }
    if (c->netif) {
        esp_netif_destroy(c->netif);
        c->netif = NULL;
    }
}

/* ================================================================
 *  dal_network_ops_t 实现
 * ================================================================ */
static dal_err_t net_init(void *ctx_, const dal_network_config_t *cfg,
                          dal_network_state_cb_t on_state, void *user_data)
{
    bsp_network_ctx_t *c = (bsp_network_ctx_t *)ctx_;
    if (c == NULL || cfg == NULL) {
        return DAL_ERR_INVALID;
    }

    c->on_state  = on_state;
    c->user_data = user_data;

    /* 1. TCP/IP 栈 + 默认事件循环 */
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        return dal_err_from_pal(ret);
    }
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return dal_err_from_pal(ret);
    }

    /* 2. Ethernet netif */
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    c->netif = esp_netif_new(&netif_cfg);
    if (c->netif == NULL) {
        return DAL_ERR_NO_MEM;
    }

    /* 3. MAC（EMAC，SMI 引脚来自板级配置） */
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.smi_gpio.mdc_num  = BOARD_NETWORK_ETH_MDC_PIN;
    emac_config.smi_gpio.mdio_num = BOARD_NETWORK_ETH_MDIO_PIN;
    c->mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (c->mac == NULL) {
        net_cleanup(c);
        return DAL_ERR_HW;
    }

    /* 4. PHY（IP101） */
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr       = BOARD_NETWORK_ETH_PHY_ADDR;
    phy_config.reset_gpio_num = BOARD_NETWORK_ETH_PHY_RESET_PIN;
    c->phy = esp_eth_phy_new_ip101(&phy_config);
    if (c->phy == NULL) {
        net_cleanup(c);
        return DAL_ERR_HW;
    }

    /* 5. 安装以太网驱动 */
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(c->mac, c->phy);
    ret = esp_eth_driver_install(&eth_config, &c->eth_handle);
    if (ret != ESP_OK) {
        net_cleanup(c);
        return dal_err_from_pal(ret);
    }

    /* 6. 绑定 netif */
    c->glue = esp_eth_new_netif_glue(c->eth_handle);
    if (c->glue == NULL) {
        net_cleanup(c);
        return DAL_ERR_NO_MEM;
    }
    ret = esp_netif_attach(c->netif, c->glue);
    if (ret != ESP_OK) {
        net_cleanup(c);
        return dal_err_from_pal(ret);
    }

    /* 7. 注册事件 */
    ret = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                     eth_event_handler, c);
    if (ret != ESP_OK) {
        net_cleanup(c);
        return dal_err_from_pal(ret);
    }
    c->eth_event_registered = true;
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                     ip_event_handler, c);
    if (ret != ESP_OK) {
        net_cleanup(c);
        return dal_err_from_pal(ret);
    }
    c->ip_event_registered = true;

    /* 8. IP 配置（DHCP 或静态） */
    if (!cfg->use_dhcp) {
        esp_netif_ip_info_t ip_info = {0};
        esp_netif_str_to_ip4(cfg->static_ip, &ip_info.ip);
        esp_netif_str_to_ip4(cfg->netmask, &ip_info.netmask);
        esp_netif_str_to_ip4(cfg->gateway, &ip_info.gw);
        ret = esp_netif_dhcpc_stop(c->netif);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            net_cleanup(c);
            return dal_err_from_pal(ret);
        }
        ret = esp_netif_set_ip_info(c->netif, &ip_info);
        if (ret != ESP_OK) {
            net_cleanup(c);
            return dal_err_from_pal(ret);
        }
    }

    /* 9. 启动 */
    ret = esp_eth_start(c->eth_handle);
    if (ret != ESP_OK) {
        net_cleanup(c);
        return dal_err_from_pal(ret);
    }
    c->started = true;
    c->inited  = true;

    PAL_LOGI("NET", "ethernet started (MDC=%d MDIO=%d PHY_ADDR=%d)",
             BOARD_NETWORK_ETH_MDC_PIN, BOARD_NETWORK_ETH_MDIO_PIN,
             BOARD_NETWORK_ETH_PHY_ADDR);
    return DAL_OK;
}

static dal_err_t net_get_ip(void *ctx_, char *ip, size_t ip_len)
{
    bsp_network_ctx_t *c = (bsp_network_ctx_t *)ctx_;
    if (c == NULL || !c->inited || c->netif == NULL || ip == NULL) {
        return DAL_ERR_INVALID;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(c->netif, &ip_info) != ESP_OK) {
        return DAL_ERR_HW;
    }
    if (ip_info.ip.addr == 0) {
        return DAL_ERR_STATE;   /* 尚未获取到 IP */
    }
    snprintf(ip, ip_len, IPSTR, IP2STR(&ip_info.ip));
    return DAL_OK;
}

static bool net_is_connected(void *ctx_)
{
    bsp_network_ctx_t *c = (bsp_network_ctx_t *)ctx_;
    return (c != NULL) && c->inited && c->connected;
}

static dal_err_t net_deinit(void *ctx_)
{
    bsp_network_ctx_t *c = (bsp_network_ctx_t *)ctx_;
    if (c == NULL || !c->inited) {
        return DAL_ERR_INVALID;
    }
    c->inited    = false;
    c->connected = false;
    net_cleanup(c);
    return DAL_OK;
}

static const dal_network_ops_t s_net_ops = {
    .init         = net_init,
    .get_ip       = net_get_ip,
    .is_connected = net_is_connected,
    .deinit       = net_deinit,
};

/* ================================================================
 *  对外初始化入口（自注册）
 * ================================================================ */
dal_err_t bsp_network_ip101_init(void)
{
    /* 用板级默认 IP 配置预初始化（DHCP） */
    dal_network_config_t cfg = {
        .use_dhcp = BOARD_NETWORK_USE_DHCP,
    };
    /* 静态 IP 字段从宏拷贝（DHCP 模式下不使用） */
    snprintf(cfg.static_ip, sizeof(cfg.static_ip), "%s", BOARD_NETWORK_STATIC_IP);
    snprintf(cfg.netmask,   sizeof(cfg.netmask),   "%s", BOARD_NETWORK_NETMASK);
    snprintf(cfg.gateway,   sizeof(cfg.gateway),   "%s", BOARD_NETWORK_GATEWAY);

    dal_err_t ret = net_init(&s_ctx, &cfg, NULL, NULL);
    if (ret != DAL_OK) {
        return ret;
    }

    /* 自注册到 DAL */
    return dal_network_register("main_eth", &s_net_ops, &s_ctx);
}
