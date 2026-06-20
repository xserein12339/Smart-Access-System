/**
 * @file    mw_event_bus.h
 * @brief   Mock mw_event_bus.h — 事件总线（宿主机测试用，空操作）
 *
 * @details service_camera 的 start/stop 会 publish 状态事件。host 测试
 *          不跑事件总线分发任务，mock 为空操作即可。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef MOCK_MW_EVENT_BUS_H
#define MOCK_MW_EVENT_BUS_H

#include "service_event.h"
#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline dal_err_t mw_event_bus_init(void) { return DAL_OK; }

static inline dal_err_t mw_event_bus_subscribe(service_event_type_t type,
                                               void (*cb)(const service_event_t *, void *),
                                               void *user_data)
{
    (void)type; (void)cb; (void)user_data;
    return DAL_OK;
}

static inline dal_err_t mw_event_bus_publish(const service_event_t *event)
{
    (void)event;
    return DAL_OK;
}

#ifdef __cplusplus
}
#endif
#endif /* MOCK_MW_EVENT_BUS_H */
