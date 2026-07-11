/**
 * @file    svc_wdt.h
 * @brief   系统稳定性保障服务 — 任务心跳监测 + 故障告警（Service 层）
 *
 * @details 订阅 SERVICE_EVT_HEARTBEAT（arg0=source, arg1=时间戳），维护各 source 的
 *          最近心跳时间。worker 周期检测超时 source，超时发 SERVICE_EVT_FAULT。
 *          本服务自身 esp_task_wdt_add/reset 喂狗，硬件看门狗由 IDF Task WDT 兜底。
 *          作为系统"保险丝"，独立于核心业务；不直接复位，致命故障经 evt_bus 广播
 *          交由上层（可后续扩展分级复位策略）。
 *
 *          依赖经 evt_bus + log_sink，不直接访问 BSP/ESP-IDF 硬件头（esp_task_wdt 除外，
 *          喂狗为 OSAL 级系统调用，非外设驱动）。
 *
 * @author  xiamu
 * @version 1.0
 */
#ifndef SVC_WDT_H
#define SVC_WDT_H

#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化看门狗服务
 *
 * 订阅 SERVICE_EVT_HEARTBEAT，创建监控 worker 任务（已 esp_task_wdt_add 喂自身）。
 * @return DAL_OK 成功，DAL_ERR_STATE 已初始化
 * @note 必须在 mw_event_bus_init / svc_log 之后、被监控业务 Service 之前调用。
 */
dal_err_t svc_wdt_init(void);

#ifdef __cplusplus
}
#endif
#endif /* SVC_WDT_H */
