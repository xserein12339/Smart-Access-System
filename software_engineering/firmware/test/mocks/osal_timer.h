/**
 * @file    osal_timer.h
 * @brief   Mock osal_timer.h — OSAL 软件定时器（宿主机测试用）
 *
 * @details 用 FFF fake 拦截 osal_timer_create/start/stop/delete，
 *          便于测试 service_camera 的 PIR 离开确认定时器逻辑。
 *          create 返回非 NULL 占位；测试可手动调保存的回调模拟超时。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef MOCK_OSAL_TIMER_H
#define MOCK_OSAL_TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include "fff.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *osal_timer_t;
typedef void (*osal_timer_cb_t)(void *arg);

#ifdef FFF_MOCK_DEFINITIONS
FAKE_VALUE_FUNC4(osal_timer_t, osal_timer_create_periodic, const char *, osal_timer_cb_t, void *, uint32_t);
FAKE_VALUE_FUNC4(osal_timer_t, osal_timer_create_one_shot, const char *, osal_timer_cb_t, void *, uint32_t);
FAKE_VALUE_FUNC1(bool, osal_timer_start, osal_timer_t);
FAKE_VALUE_FUNC1(bool, osal_timer_stop, osal_timer_t);
FAKE_VOID_FUNC1(osal_timer_delete, osal_timer_t);
FAKE_VALUE_FUNC1(bool, osal_timer_is_running, osal_timer_t);
#else
DECLARE_FAKE_VALUE_FUNC4(osal_timer_t, osal_timer_create_periodic, const char *, osal_timer_cb_t, void *, uint32_t);
DECLARE_FAKE_VALUE_FUNC4(osal_timer_t, osal_timer_create_one_shot, const char *, osal_timer_cb_t, void *, uint32_t);
DECLARE_FAKE_VALUE_FUNC1(bool, osal_timer_start, osal_timer_t);
DECLARE_FAKE_VALUE_FUNC1(bool, osal_timer_stop, osal_timer_t);
DECLARE_FAKE_VOID_FUNC1(osal_timer_delete, osal_timer_t);
DECLARE_FAKE_VALUE_FUNC1(bool, osal_timer_is_running, osal_timer_t);
#endif /* FFF_MOCK_DEFINITIONS */

#ifdef __cplusplus
}
#endif
#endif /* MOCK_OSAL_TIMER_H */
