/**
 * @file    osal_task.h
 * @brief   Mock osal_task.h — OSAL 任务接口（宿主机测试用）
 *
 * @details host 测试不起真实任务：
 *          - delay_ms 空操作（避免阻塞测试）
 *          - task_create 返回非 NULL 占位（不实际创建任务）
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef MOCK_OSAL_TASK_H
#define MOCK_OSAL_TASK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *osal_task_handle_t;
typedef void (*osal_task_func_t)(void *arg);

static inline osal_task_handle_t osal_task_create(const char *name, osal_task_func_t func,
                                                  void *arg, uint32_t stack_size,
                                                  int priority, int core_id)
{
    (void)name; (void)func; (void)arg; (void)stack_size; (void)priority; (void)core_id;
    return (osal_task_handle_t)1;
}

static inline void osal_task_delete(osal_task_handle_t task)
{
    (void)task;
}

static inline void osal_task_delay_ms(uint32_t ms)
{
    (void)ms;
}

static inline uint32_t osal_task_get_stack_high_water_mark(osal_task_handle_t task)
{
    (void)task;
    return 0;
}

#ifdef __cplusplus
}
#endif
#endif /* MOCK_OSAL_TASK_H */
