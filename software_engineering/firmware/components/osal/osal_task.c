/**
 * @file    osal_task.c
 * @brief   OSAL Task implementation (FreeRTOS backend)
 */

#include "osal_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

osal_task_handle_t osal_task_create(const char *name, osal_task_func_t func,
                                    void *arg, uint32_t stack_size,
                                    int priority, int core_id)
{
    TaskHandle_t handle = NULL;
    BaseType_t ret;

    if (core_id < 0) {
        ret = xTaskCreate(func, name, stack_size, arg, priority, &handle);
    } else {
        ret = xTaskCreatePinnedToCore(func, name, stack_size, arg,
                                      priority, &handle, core_id);
    }

    return (ret == pdPASS) ? (osal_task_handle_t)handle : NULL;
}

void osal_task_delete(osal_task_handle_t task)
{
    if (task != NULL) {
        vTaskDelete((TaskHandle_t)task);
    }
}

void osal_task_delay_ms(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    /* pdMS_TO_TICKS 可能截断为 0（如 FreeRTOS tick 100Hz 时 <10ms → 0 ticks），
     * vTaskDelay(0) 仅释放剩余时间片而不阻塞，不会触发任务调度，导致同核
     * IDLE 任务饥饿 → TWDT 复位。至少保证 1 tick 的阻塞延迟。 */
    if (ticks == 0) ticks = 1;
    vTaskDelay(ticks);
}

uint32_t osal_task_get_stack_high_water_mark(osal_task_handle_t task)
{
    TaskHandle_t handle = (task != NULL) ? (TaskHandle_t)task : xTaskGetCurrentTaskHandle();
    return uxTaskGetStackHighWaterMark(handle);
}
