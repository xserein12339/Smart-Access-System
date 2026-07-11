/**
 * @file    svc_ota.h
 * @brief   OTA 固件升级服务 — HTTP 下载 + flash 烧写 + 重启（Service 层）
 *
 * @details 职责：接收 SERVICE_EVT_CMD_OTA 事件 → esp_http_client 下载固件
 *          → esp_ota_begin/write/end 烧写 → esp_ota_set_boot_partition 切换槽位
 *          → esp_restart 重启。下载过程中每 5% 发布 SERVICE_EVT_OTA_PROGRESS，
 *          结束时发布 SERVICE_EVT_OTA_RESULT。
 *
 *          不依赖任何 DAL 硬件 ops，仅依赖中间件（event_bus）和 ESP-IDF
 *          协议栈（esp_http_client / esp_ota / mbedtls）。
 *
 *          SHA256：流式计算，下载同时用 mbedtls_sha256 计算摘要，
 *          esp_ota_end 后与预期值比较，不匹配则 abort。
 *
 * @author  xiamu
 * @version 1.0
 */
#ifndef SVC_OTA_H
#define SVC_OTA_H

#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA 服务依赖契约（可传 NULL，无硬件依赖）
 */
typedef struct {
    void *reserved;  /**< 预留扩展 */
} svc_ota_deps_t;

/**
 * @brief 初始化 OTA 服务（订阅事件 + 创建 worker 任务）
 * @param[in] deps 可传 NULL
 * @return DAL_OK 成功，DAL_ERR_STATE 已初始化
 * @note 必须在 mw_event_bus_init 之后调用。
 */
dal_err_t svc_ota_init(const svc_ota_deps_t *deps);

#ifdef __cplusplus
}
#endif
#endif /* SVC_OTA_H */
