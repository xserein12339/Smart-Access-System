/**
 * @file    pal_wdt.c
 * @brief   PAL WDT 模块 - 实现（ESP-IDF esp_task_wdt 封装）
 *
 * 直接映射到 ESP-IDF TWDT API。用户句柄内部即 esp_task_wdt_user_handle_t
 * （其本身已是不透明指针），无需额外封装结构体。
 */

#include "pal_wdt.h"

#include "esp_task_wdt.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ================================================================
 *  全局 TWDT API
 * ================================================================ */

int pal_wdt_init(const pal_wdt_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = cfg->timeout_ms,
        .idle_core_mask = cfg->idle_core_mask,
        .trigger_panic  = cfg->trigger_panic,
    };
    return esp_task_wdt_init(&wdt_cfg);
}

int pal_wdt_reconfigure(const pal_wdt_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = cfg->timeout_ms,
        .idle_core_mask = cfg->idle_core_mask,
        .trigger_panic  = cfg->trigger_panic,
    };
    return esp_task_wdt_reconfigure(&wdt_cfg);
}

int pal_wdt_deinit(void)
{
    return esp_task_wdt_deinit();
}

/* ================================================================
 *  用户订阅 API
 * ================================================================ */

int pal_wdt_add_user(const char *name, pal_wdt_user_handle_t *handle)
{
    if (name == NULL || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_task_wdt_user_handle_t user = NULL;
    esp_err_t ret = esp_task_wdt_add_user(name, &user);
    if (ret != ESP_OK) {
        return ret;
    }
    *handle = (pal_wdt_user_handle_t)user;
    return ESP_OK;
}

int pal_wdt_reset_user(pal_wdt_user_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_task_wdt_reset_user((esp_task_wdt_user_handle_t)handle);
}

int pal_wdt_delete_user(pal_wdt_user_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_task_wdt_delete_user((esp_task_wdt_user_handle_t)handle);
}

/* ================================================================
 *  任务订阅 API
 * ================================================================ */

int pal_wdt_add_task(void)
{
    return esp_task_wdt_add(xTaskGetCurrentTaskHandle());
}

int pal_wdt_reset(void)
{
    return esp_task_wdt_reset();
}
