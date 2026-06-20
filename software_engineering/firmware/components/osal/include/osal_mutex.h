/**
 * @file    osal_mutex.h
 * @brief   OSAL Mutex API
 *
 * Wraps FreeRTOS xSemaphoreCreateMutex.
 * Mutexes provide mutual exclusion with priority inheritance.
 */

#ifndef OSAL_MUTEX_H
#define OSAL_MUTEX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Opaque handle ---- */
typedef void *osal_mutex_t;

/**
 * @brief Create a mutex
 *
 * @return Mutex handle, or NULL on failure
 */
osal_mutex_t osal_mutex_create(void);

/**
 * @brief Lock mutex (blocking)
 *
 * @param mutex      Mutex handle
 * @param timeout_ms Timeout in ms, 0 = no wait, portMAX_DELAY = block forever
 * @return           true if locked successfully, false on timeout
 */
bool osal_mutex_lock(osal_mutex_t mutex, uint32_t timeout_ms);

/**
 * @brief Unlock mutex
 *
 * @param mutex Mutex handle
 */
void osal_mutex_unlock(osal_mutex_t mutex);

/**
 * @brief Delete mutex and free resources
 *
 * @param mutex Mutex handle
 */
void osal_mutex_delete(osal_mutex_t mutex);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_MUTEX_H */
