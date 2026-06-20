/**
 * @file    osal_semaphore.h
 * @brief   Mock osal_semaphore.h — OSAL 信号量（宿主机测试用）
 *
 * @details 用 FFF fake 拦截 osal_sem_create/take/give/delete，
 *          便于测试 buffer_pool 的 alloc 阻塞/超时与 ref/unref 回收逻辑。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef MOCK_OSAL_SEMAPHORE_H
#define MOCK_OSAL_SEMAPHORE_H

#include <stdint.h>
#include <stdbool.h>
#include "fff.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *osal_sem_t;

#ifdef FFF_MOCK_DEFINITIONS
FAKE_VALUE_FUNC0(osal_sem_t, osal_sem_create_binary);
FAKE_VALUE_FUNC2(osal_sem_t, osal_sem_create_counting, uint32_t, uint32_t);
FAKE_VALUE_FUNC2(bool, osal_sem_take, osal_sem_t, uint32_t);
FAKE_VALUE_FUNC1(bool, osal_sem_give, osal_sem_t);
FAKE_VOID_FUNC1(osal_sem_delete, osal_sem_t);
#else
DECLARE_FAKE_VALUE_FUNC0(osal_sem_t, osal_sem_create_binary);
DECLARE_FAKE_VALUE_FUNC2(osal_sem_t, osal_sem_create_counting, uint32_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC2(bool, osal_sem_take, osal_sem_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC1(bool, osal_sem_give, osal_sem_t);
DECLARE_FAKE_VOID_FUNC1(osal_sem_delete, osal_sem_t);
#endif /* FFF_MOCK_DEFINITIONS */

#ifdef __cplusplus
}
#endif
#endif /* MOCK_OSAL_SEMAPHORE_H */
