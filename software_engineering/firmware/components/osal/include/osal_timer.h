/**
 * @file    osal_timer.h
 * @brief   OSAL Software Timer API
 *
 * Wraps ESP-IDF esp_timer, which provides microsecond-resolution timers.
 * These are software timers, NOT hardware timers — suitable for
 * non-hard-real-time periodic tasks.
 */

#ifndef OSAL_TIMER_H
#define OSAL_TIMER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Opaque handle ---- */
typedef void *osal_timer_t;

/* ---- Timer callback ---- */
typedef void (*osal_timer_cb_t)(void *arg);

/**
 * @brief Create a periodic (repeating) timer
 *
 * @param name      Timer name for debugging
 * @param cb        Callback function (called from timer task context)
 * @param arg       User argument passed to callback
 * @param period_ms Timer period in milliseconds
 * @return          Timer handle, or NULL on failure
 */
osal_timer_t osal_timer_create_periodic(const char *name, osal_timer_cb_t cb,
                                        void *arg, uint32_t period_ms);

/**
 * @brief Create a one-shot timer
 *
 * @param name      Timer name for debugging
 * @param cb        Callback function
 * @param arg       User argument
 * @param delay_ms  Delay before firing in milliseconds
 * @return          Timer handle, or NULL on failure
 */
osal_timer_t osal_timer_create_one_shot(const char *name, osal_timer_cb_t cb,
                                        void *arg, uint32_t delay_ms);

/**
 * @brief Start or restart a timer
 *
 * @param timer Timer handle
 * @return      true on success
 */
bool osal_timer_start(osal_timer_t timer);

/**
 * @brief Stop a running timer
 *
 * @param timer Timer handle
 * @return      true on success
 */
bool osal_timer_stop(osal_timer_t timer);

/**
 * @brief Delete timer and free resources
 *
 * @param timer Timer handle
 */
void osal_timer_delete(osal_timer_t timer);

/**
 * @brief Check if timer is currently running
 *
 * @param timer Timer handle
 * @return      true if running
 */
bool osal_timer_is_running(osal_timer_t timer);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_TIMER_H */
