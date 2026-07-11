/**
 * @file    mw_event_bus.h
 * @brief   事件总线中间件 — 发布/订阅解耦 Service 间控制流
 *
 * @details 单实例事件总线：publish 将事件入 FreeRTOS 队列后立即返回（非阻塞语义），
 *          内部分发任务出队后按事件类型匹配订阅者回调。订阅者以
 *          SERVICE_EVT_NONE 订阅全部事件。
 *
 *          线程安全：订阅表操作经 mutex 保护；publish 仅 xQueueSend。
 *          分发回调运行在分发任务上下文（非 ISR），可执行较复杂逻辑，
 *          但应避免长时间阻塞以免影响后续事件分发。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef MW_EVENT_BUS_H
#define MW_EVENT_BUS_H

#include "service_event.h"
#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 订阅者回调原型 */
typedef void (*mw_event_bus_cb_t)(const service_event_t *event, void *user_data);

/**
 * @brief 初始化事件总线（创建队列/互斥/分发任务）
 * @return DAL_OK 成功，DAL_ERR_STATE 已初始化，DAL_ERR_NO_MEM 资源创建失败
 * @note 必须在任何 subscribe/publish 之前、各 Service init 之前调用一次。
 */
dal_err_t mw_event_bus_init(void);

/**
 * @brief 订阅事件
 * @param[in] type      事件类型，SERVICE_EVT_NONE 表示订阅全部
 * @param[in] cb        回调（不可为 NULL）
 * @param[in] user_data 透传给回调的用户数据
 * @return DAL_OK 成功，DAL_ERR_INVALID cb 为空，DAL_ERR_NO_MEM 订阅表满
 */
dal_err_t mw_event_bus_subscribe(service_event_type_t type,
                                mw_event_bus_cb_t cb,
                                void *user_data);

/**
 * @brief 发布事件（入队后立即返回，由分发任务异步回调）
 * @param[in] event 事件指针（按值拷贝入队，data 指针生命周期由发布者保证）
 * @return DAL_OK 成功，DAL_ERR_INVALID event 为空或 type 为 NONE，DAL_ERR_BUSY 队列满
 * @note 非阻塞，可在任务上下文调用；ISR 中禁止调用（内部用 xQueueSend 带超时参数的 API）。
 */
dal_err_t mw_event_bus_publish(const service_event_t *event);

#ifdef __cplusplus
}
#endif
#endif /* MW_EVENT_BUS_H */
