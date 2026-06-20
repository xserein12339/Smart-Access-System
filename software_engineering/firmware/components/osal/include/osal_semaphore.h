/**
 * @file    osal_semaphore.h
 * @brief   OSAL Semaphore API
 *
 * Wraps FreeRTOS xSemaphoreCreateBinary / xSemaphoreCreateCounting.
 */

#ifndef OSAL_SEMAPHORE_H
#define OSAL_SEMAPHORE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Opaque handle ---- */
typedef void *osal_sem_t;

/**
 * @brief Create a binary semaphore (initially not taken)
 *
 * @return Semaphore handle, or NULL on failure
 */
osal_sem_t osal_sem_create_binary(void);

/**
 * @brief Create a counting semaphore
 *
 * @param max_count Maximum count value
 * @param init_count Initial count value
 * @return Semaphore handle, or NULL on failure
 */
osal_sem_t osal_sem_create_counting(uint32_t max_count, uint32_t init_count);

/**
 * @brief Take (acquire) a semaphore
 *
 * @param sem        Semaphore handle
 * @param timeout_ms Timeout in ms, 0 = no wait, portMAX_DELAY = block forever
 * @return           true if taken, false on timeout
 */
bool osal_sem_take(osal_sem_t sem, uint32_t timeout_ms);

/**
 * @brief Give (release) a semaphore
 *
 * @param sem Semaphore handle
 * @return    true on success
 */
bool osal_sem_give(osal_sem_t sem);

/**
 * @brief Delete semaphore and free resources
 *
 * @param sem Semaphore handle
 */
void osal_sem_delete(osal_sem_t sem);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_SEMAPHORE_H */
