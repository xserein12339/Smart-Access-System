/**
 * @file    osal_task.h
 * @brief   OSAL Task Management API
 *
 * Wraps FreeRTOS xTaskCreate / vTaskDelete / vTaskDelay.
 */

#ifndef OSAL_TASK_H
#define OSAL_TASK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Opaque handle ---- */
typedef void *osal_task_handle_t;

/* ---- Task function signature ---- */
typedef void (*osal_task_func_t)(void *arg);

/**
 * @brief Create a task pinned to a specific CPU core
 *
 * @param name       Task name (for debugging, max 16 chars)
 * @param func       Task entry function
 * @param arg        Argument passed to task function
 * @param stack_size Stack size in bytes
 * @param priority   FreeRTOS priority (0 = idle, higher = more urgent)
 * @param core_id    CPU core to pin to (0 or 1), or -1 for no affinity
 * @return           Task handle, or NULL on failure
 */
osal_task_handle_t osal_task_create(const char *name, osal_task_func_t func,
                                    void *arg, uint32_t stack_size,
                                    int priority, int core_id);

/**
 * @brief Delete a task
 *
 * @param task Task handle from osal_task_create()
 */
void osal_task_delete(osal_task_handle_t task);

/**
 * @brief Delay current task for specified milliseconds
 *
 * @param ms Delay duration in milliseconds
 */
void osal_task_delay_ms(uint32_t ms);

/**
 * @brief Get the minimum free stack size ever seen for this task
 *
 * @param task Task handle, NULL for current task
 * @return     Minimum free stack in bytes
 */
uint32_t osal_task_get_stack_high_water_mark(osal_task_handle_t task);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_TASK_H */
