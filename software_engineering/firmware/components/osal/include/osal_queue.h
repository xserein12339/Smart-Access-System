/**
 * @file    osal_queue.h
 * @brief   OSAL Message Queue API
 *
 * Wraps FreeRTOS xQueueCreate / xQueueSend / xQueueReceive.
 */

#ifndef OSAL_QUEUE_H
#define OSAL_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Opaque handle ---- */
typedef void *osal_queue_t;

/**
 * @brief Create a message queue
 *
 * @param item_size Size of each queue item in bytes
 * @param item_count Maximum number of items in queue
 * @return Queue handle, or NULL on failure
 */
osal_queue_t osal_queue_create(uint32_t item_size, uint32_t item_count);

/**
 * @brief Send an item to the queue (blocking)
 *
 * @param queue      Queue handle
 * @param item       Pointer to data to send (copied into queue)
 * @param timeout_ms Timeout in ms, 0 = no wait, portMAX_DELAY = block forever
 * @return           true if sent, false on timeout or queue full
 */
bool osal_queue_send(osal_queue_t queue, const void *item, uint32_t timeout_ms);

/**
 * @brief Receive an item from the queue (blocking)
 *
 * @param queue      Queue handle
 * @param item       Pointer to buffer for received data
 * @param timeout_ms Timeout in ms, 0 = no wait, portMAX_DELAY = block forever
 * @return           true if received, false on timeout
 */
bool osal_queue_receive(osal_queue_t queue, void *item, uint32_t timeout_ms);

/**
 * @brief Get the number of messages currently in the queue
 *
 * @param queue Queue handle
 * @return      Number of queued items
 */
uint32_t osal_queue_get_count(osal_queue_t queue);

/**
 * @brief Delete queue and free resources
 *
 * @param queue Queue handle
 */
void osal_queue_delete(osal_queue_t queue);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_QUEUE_H */
