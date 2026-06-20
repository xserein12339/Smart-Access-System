/**
 * @file    osal_mutex.c
 * @brief   OSAL Mutex implementation (FreeRTOS backend)
 */

#include "osal_mutex.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

osal_mutex_t osal_mutex_create(void)
{
    SemaphoreHandle_t handle = xSemaphoreCreateMutex();
    return (osal_mutex_t)handle;
}

bool osal_mutex_lock(osal_mutex_t mutex, uint32_t timeout_ms)
{
    if (mutex == NULL) {
        return false;
    }
    return (xSemaphoreTake((SemaphoreHandle_t)mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

void osal_mutex_unlock(osal_mutex_t mutex)
{
    if (mutex != NULL) {
        xSemaphoreGive((SemaphoreHandle_t)mutex);
    }
}

void osal_mutex_delete(osal_mutex_t mutex)
{
    if (mutex != NULL) {
        vSemaphoreDelete((SemaphoreHandle_t)mutex);
    }
}
