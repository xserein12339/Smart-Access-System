/**
 * @file    dal_registry.c
 * @brief   DAL 通用设备注册表实现
 *
 * @author  xLumina
 * @version 1.0
 */
#include "dal_registry.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

/** 注册表互斥锁等待超时（ms）；pdMS_TO_TICKS 转换，
 *  传 portMAX_DELAY 会溢出，故用有限超时 */
#define DAL_REGISTRY_LOCK_TIMEOUT_MS 1000u

/** 各注册表共享一把懒创建的 mutex —— 简化模型：注册表数量有限且
 *  get/register 不频繁，单把全局锁足够，避免每个 registry 各持一把。 */
static SemaphoreHandle_t s_mutex = NULL;
static bool              s_mutex_created = false;

static dal_err_t registry_ensure_mutex(void)
{
    if (!s_mutex_created) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return DAL_ERR_NO_MEM;
        }
        s_mutex_created = true;
    }
    return DAL_OK;
}

dal_err_t dal_registry_init(dal_registry_t *reg,
                            dal_registry_entry_t *entries,
                            uint8_t max)
{
    if (reg == NULL || entries == NULL || max == 0) {
        return DAL_ERR_INVALID;
    }
    memset(entries, 0, sizeof(dal_registry_entry_t) * max);
    reg->entries       = entries;
    reg->max           = max;
    reg->count         = 0;
    reg->mutex_created = false;   /* mutex 全局共享，此标志仅用于语义占位 */
    return DAL_OK;
}

dal_err_t dal_registry_register(dal_registry_t *reg,
                                const char *name,
                                const void *ops,
                                void *ctx)
{
    if (reg == NULL || name == NULL || ops == NULL) {
        return DAL_ERR_INVALID;
    }

    dal_err_t ret = registry_ensure_mutex();
    if (ret != DAL_OK) {
        return ret;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(DAL_REGISTRY_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return DAL_ERR_BUSY;
    }

    if (reg->count >= reg->max) {
        xSemaphoreGive(s_mutex);
        return DAL_ERR_NO_MEM;
    }

    for (uint8_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->entries[i].name, name) == 0) {
            xSemaphoreGive(s_mutex);
            return DAL_ERR_STATE;   /* 名称冲突 */
        }
    }

    reg->entries[reg->count].name = name;
    reg->entries[reg->count].ops  = ops;
    reg->entries[reg->count].ctx  = ctx;
    reg->count++;

    xSemaphoreGive(s_mutex);
    return DAL_OK;
}

dal_err_t dal_registry_get(dal_registry_t *reg,
                           const char *name,
                           const void **ops,
                           void **ctx)
{
    if (reg == NULL || name == NULL || ops == NULL || ctx == NULL) {
        return DAL_ERR_INVALID;
    }

    *ops = NULL;
    *ctx = NULL;

    if (!s_mutex_created) {
        /* 注册表尚未有任何实例注册过 */
        return DAL_ERR_NOT_FOUND;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(DAL_REGISTRY_LOCK_TIMEOUT_MS)) != pdTRUE) {
        return DAL_ERR_BUSY;
    }

    for (uint8_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->entries[i].name, name) == 0) {
            *ops = reg->entries[i].ops;
            *ctx = reg->entries[i].ctx;
            xSemaphoreGive(s_mutex);
            return DAL_OK;
        }
    }

    xSemaphoreGive(s_mutex);
    return DAL_ERR_NOT_FOUND;
}
