/**
 * @file    osal_timer.c
 * @brief   OSAL Timer implementation (ESP-IDF esp_timer backend)
 */

#include "osal_timer.h"

#include "esp_timer.h"

#include <stdlib.h>

/* ---- Internal timer wrapper structure ---- */
typedef struct {
    esp_timer_handle_t handle;
    osal_timer_cb_t    callback;
    void              *user_arg;
    bool               is_periodic;
} osal_timer_internal_t;

static void timer_callback_wrapper(void *arg)
{
    osal_timer_internal_t *timer = (osal_timer_internal_t *)arg;
    if (timer != NULL && timer->callback != NULL) {
        timer->callback(timer->user_arg);
    }
}

osal_timer_t osal_timer_create_periodic(const char *name, osal_timer_cb_t cb,
                                        void *arg, uint32_t period_ms)
{
    osal_timer_internal_t *timer = calloc(1, sizeof(osal_timer_internal_t));
    if (timer == NULL) {
        return NULL;
    }

    timer->callback    = cb;
    timer->user_arg    = arg;
    timer->is_periodic = true;

    esp_timer_create_args_t cfg = {
        .callback    = timer_callback_wrapper,
        .arg         = timer,
        .dispatch_method = ESP_TIMER_TASK,
        .name        = name,
        .skip_unhandled_events = false,
    };

    esp_err_t ret = esp_timer_create(&cfg, &timer->handle);
    if (ret != ESP_OK) {
        free(timer);
        return NULL;
    }

    /* Start immediately */
    esp_timer_start_periodic(timer->handle, period_ms * 1000);

    return (osal_timer_t)timer;
}

osal_timer_t osal_timer_create_one_shot(const char *name, osal_timer_cb_t cb,
                                        void *arg, uint32_t delay_ms)
{
    osal_timer_internal_t *timer = calloc(1, sizeof(osal_timer_internal_t));
    if (timer == NULL) {
        return NULL;
    }

    timer->callback    = cb;
    timer->user_arg    = arg;
    timer->is_periodic = false;

    esp_timer_create_args_t cfg = {
        .callback    = timer_callback_wrapper,
        .arg         = timer,
        .dispatch_method = ESP_TIMER_TASK,
        .name        = name,
        .skip_unhandled_events = false,
    };

    esp_err_t ret = esp_timer_create(&cfg, &timer->handle);
    if (ret != ESP_OK) {
        free(timer);
        return NULL;
    }

    /* Start one-shot */
    esp_timer_start_once(timer->handle, delay_ms * 1000);

    return (osal_timer_t)timer;
}

bool osal_timer_start(osal_timer_t timer)
{
    if (timer == NULL) {
        return false;
    }
    osal_timer_internal_t *t = (osal_timer_internal_t *)timer;
    return (esp_timer_start_periodic(t->handle, 0) == ESP_OK);
}

bool osal_timer_stop(osal_timer_t timer)
{
    if (timer == NULL) {
        return false;
    }
    osal_timer_internal_t *t = (osal_timer_internal_t *)timer;
    return (esp_timer_stop(t->handle) == ESP_OK);
}

void osal_timer_delete(osal_timer_t timer)
{
    if (timer == NULL) {
        return;
    }
    osal_timer_internal_t *t = (osal_timer_internal_t *)timer;
    esp_timer_stop(t->handle);
    esp_timer_delete(t->handle);
    free(t);
}

bool osal_timer_is_running(osal_timer_t timer)
{
    if (timer == NULL) {
        return false;
    }
    osal_timer_internal_t *t = (osal_timer_internal_t *)timer;
    return esp_timer_is_active(t->handle);
}
