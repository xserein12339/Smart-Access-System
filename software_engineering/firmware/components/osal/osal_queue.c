/**
 * @file    osal_queue.c
 * @brief   OSAL Message Queue implementation (FreeRTOS backend)
 */

#include "osal_queue.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

osal_queue_t osal_queue_create(uint32_t item_size, uint32_t item_count)
{
    QueueHandle_t handle = xQueueCreate(item_count, item_size);
    return (osal_queue_t)handle;
}

bool osal_queue_send(osal_queue_t queue, const void *item, uint32_t timeout_ms)
{
    if (queue == NULL || item == NULL) {
        return false;
    }
    return (xQueueSend((QueueHandle_t)queue, item, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

bool osal_queue_receive(osal_queue_t queue, void *item, uint32_t timeout_ms)
{
    if (queue == NULL || item == NULL) {
        return false;
    }
    return (xQueueReceive((QueueHandle_t)queue, item, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

uint32_t osal_queue_get_count(osal_queue_t queue)
{
    if (queue == NULL) {
        return 0;
    }
    return (uint32_t)uxQueueMessagesWaiting((QueueHandle_t)queue);
}

void osal_queue_delete(osal_queue_t queue)
{
    if (queue != NULL) {
        vQueueDelete((QueueHandle_t)queue);
    }
}
