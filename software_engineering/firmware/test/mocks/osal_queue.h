/**
 * @file    osal_queue.h
 * @brief   Mock osal_queue.h — OSAL 消息队列（宿主机测试用）
 *
 * @details host 测试不跑真实 FreeRTOS 队列/任务。用 FFF fake 拦截
 *          osal_queue_create/send/receive，便于测试 publish 入队逻辑。
 *          osal_queue_create 返回非 NULL 占位。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef MOCK_OSAL_QUEUE_H
#define MOCK_OSAL_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "fff.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *osal_queue_t;

#ifdef FFF_MOCK_DEFINITIONS
FAKE_VALUE_FUNC2(osal_queue_t, osal_queue_create, uint32_t, uint32_t);
FAKE_VALUE_FUNC3(bool, osal_queue_send, osal_queue_t, const void *, uint32_t);
FAKE_VALUE_FUNC3(bool, osal_queue_receive, osal_queue_t, void *, uint32_t);
FAKE_VALUE_FUNC1(uint32_t, osal_queue_get_count, osal_queue_t);
FAKE_VOID_FUNC1(osal_queue_delete, osal_queue_t);
#else
DECLARE_FAKE_VALUE_FUNC2(osal_queue_t, osal_queue_create, uint32_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC3(bool, osal_queue_send, osal_queue_t, const void *, uint32_t);
DECLARE_FAKE_VALUE_FUNC3(bool, osal_queue_receive, osal_queue_t, void *, uint32_t);
DECLARE_FAKE_VALUE_FUNC1(uint32_t, osal_queue_get_count, osal_queue_t);
DECLARE_FAKE_VOID_FUNC1(osal_queue_delete, osal_queue_t);
#endif /* FFF_MOCK_DEFINITIONS */

#ifdef __cplusplus
}
#endif
#endif /* MOCK_OSAL_QUEUE_H */
