/**
 * @file    dal_relay.c
 * @brief   继电器 DAL 实例管理（纯软件，无硬件依赖）
 *
 * @details 维护继电器实例注册表，提供按名称注册与原子查询。
 *          注册表用 OSAL 互斥锁保护，register/get 线程安全。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */
#include "dal_relay.h"
#include "osal_mutex.h"
#include <string.h>
#include <stddef.h>

#ifndef DAL_RELAY_MAX_INSTANCES
#define DAL_RELAY_MAX_INSTANCES  8   /**< 可由编译选项覆盖 */
#endif

/** 注册表互斥锁等待超时（ms）；OSAL lock 经 pdMS_TO_TICKS 转换，
 *  传 portMAX_DELAY 会溢出，故用有限超时，拿不到锁返回 DAL_ERR_BUSY */
#define DAL_RELAY_LOCK_TIMEOUT_MS  1000u

typedef struct {
    const char            *name;
    const dal_relay_ops_t *ops;
    void                  *ctx;
} dal_relay_entry_t;

/* 静态实例表，避免动态内存分配 */
static dal_relay_entry_t s_entries[DAL_RELAY_MAX_INSTANCES];
static uint8_t           s_count  = 0;
static osal_mutex_t      s_mutex  = NULL;

/**
 * @brief 懒创建注册表互斥锁
 *
 * @note 依赖 ESP-IDF 启动模型：app_main 单线程顺序调用各 bsp_xxx_init，
 *       首次 register 不会并发，故此处懒创建无需额外同步。
 *       后续 get 可能在多任务并发，此时 s_mutex 已就绪。
 *
 * @return DAL_OK 成功，DAL_ERR_NO_MEM 创建失败
 */
static dal_err_t relay_ensure_mutex(void)
{
    if (s_mutex == NULL) {
        s_mutex = osal_mutex_create();
        if (s_mutex == NULL) {
            return DAL_ERR_NO_MEM;
        }
    }
    return DAL_OK;
}

dal_err_t dal_relay_register(const char *name,
                             const dal_relay_ops_t *ops,
                             void *ctx)
{
    if (name == NULL || ops == NULL) {
        return DAL_ERR_INVALID;
    }

    dal_err_t ret = relay_ensure_mutex();
    if (ret != DAL_OK) {
        return ret;
    }

    if (!osal_mutex_lock(s_mutex, DAL_RELAY_LOCK_TIMEOUT_MS)) {
        return DAL_ERR_BUSY;
    }

    if (s_count >= DAL_RELAY_MAX_INSTANCES) {
        osal_mutex_unlock(s_mutex);
        return DAL_ERR_NO_MEM;
    }

    /* 防止重复注册 */
    for (uint8_t i = 0; i < s_count; i++) {
        if (strcmp(s_entries[i].name, name) == 0) {
            osal_mutex_unlock(s_mutex);
            return DAL_ERR_STATE;
        }
    }

    s_entries[s_count].name = name;
    s_entries[s_count].ops  = ops;
    s_entries[s_count].ctx  = ctx;
    s_count++;

    osal_mutex_unlock(s_mutex);
    return DAL_OK;
}

dal_err_t dal_relay_get(const char *name,
                        const dal_relay_ops_t **ops,
                        void **ctx)
{
    if (name == NULL || ops == NULL || ctx == NULL) {
        return DAL_ERR_INVALID;
    }

    *ops = NULL;
    *ctx = NULL;

    if (s_mutex == NULL) {
        /* 注册表尚未初始化（无任何实例注册过） */
        return DAL_ERR_NOT_FOUND;
    }

    if (!osal_mutex_lock(s_mutex, DAL_RELAY_LOCK_TIMEOUT_MS)) {
        return DAL_ERR_BUSY;
    }

    for (uint8_t i = 0; i < s_count; i++) {
        if (strcmp(s_entries[i].name, name) == 0) {
            *ops = s_entries[i].ops;
            *ctx = s_entries[i].ctx;
            osal_mutex_unlock(s_mutex);
            return DAL_OK;
        }
    }

    osal_mutex_unlock(s_mutex);
    return DAL_ERR_NOT_FOUND;
}
