/**
 * @file    osal_semaphore.c
 * @brief   OSAL Semaphore implementation (FreeRTOS backend)
 */

#include "osal_semaphore.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

osal_sem_t osal_sem_create_binary(void)
{
    SemaphoreHandle_t handle = xSemaphoreCreateBinary();
    return (osal_sem_t)handle;
}

osal_sem_t osal_sem_create_counting(uint32_t max_count, uint32_t init_count)
{
    SemaphoreHandle_t handle = xSemaphoreCreateCounting(max_count, init_count);
    return (osal_sem_t)handle;
}

bool osal_sem_take(osal_sem_t sem, uint32_t timeout_ms)
{
    if (sem == NULL) {
        return false;
    }
    return (xSemaphoreTake((SemaphoreHandle_t)sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

bool osal_sem_give(osal_sem_t sem)
{
    if (sem == NULL) {
        return false;
    }
    return (xSemaphoreGive((SemaphoreHandle_t)sem) == pdTRUE);
}

void osal_sem_delete(osal_sem_t sem)
{
    if (sem != NULL) {
        vSemaphoreDelete((SemaphoreHandle_t)sem);
    }
}
