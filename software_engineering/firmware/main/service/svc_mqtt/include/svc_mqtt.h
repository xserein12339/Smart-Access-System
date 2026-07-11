/**
 * @file    svc_mqtt.h
 * @brief   MQTT 通信服务 — 纯通信管道 + OTA 命令路由（Service 层）
 *
 * @details 下行：按 topic 后缀解析 JSON → service_downlink_cmd_t 或
 *          service_ota_cmd_t → 经事件总线发布。**不做业务决策**。
 *          上行：订阅业务事件（AUTH/RECOG/FAULT/CMD_ACK/OTA），worker 序列化
 *          JSON 上报云端。
 *
 *          通信契约见 software_engineering/web/docs/PROTOCOL.md。
 *          架构例外：本服务需含 esp-mqtt（mqtt_client.h）与 cJSON（cjson.h），
 *          不含任何 driver/bsp 硬件头。
 *
 * @author  xiamu
 * @version 1.1
 */
#ifndef SVC_MQTT_H
#define SVC_MQTT_H

#include "dal_err.h"
#include "dal_network_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const dal_network_ops_t *network_ops;
    void                    *network_ctx;
} svc_mqtt_deps_t;

dal_err_t svc_mqtt_init(const svc_mqtt_deps_t *deps);

dal_err_t svc_mqtt_start(void);

dal_err_t svc_mqtt_stop(void);

#ifdef __cplusplus
}
#endif
#endif /* SVC_MQTT_H */
