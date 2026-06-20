/**
 * @file    osal_mutex.h
 * @brief   Mock osal_mutex.h — OSAL 互斥锁（宿主机测试用）
 *
 * @details 拦截被测 DAL 代码对真实 OSAL（FreeRTOS）互斥锁的依赖。
 *          host 单线程测试无需真实互斥，mock 为空操作：
 *          - create 返回非 NULL 占位指针（使懒创建判断通过）
 *          - lock 永远返回 true
 *          - unlock/delete 空操作
 *
 *          签名与真实 osal_mutex.h 完全一致，仅实现替换。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef MOCK_OSAL_MUTEX_H
#define MOCK_OSAL_MUTEX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** OSAL 互斥锁句柄（不透明，与真实定义一致为 void*） */
typedef void *osal_mutex_t;

/** 占位超时常量（真实 OSAL 中存在，dal_relay.c 可能用） */
#ifndef OSAL_WAIT_FOREVER
#define OSAL_WAIT_FOREVER 0xFFFFFFFFu
#endif

/** @brief 创建互斥锁（mock：返回非 NULL 占位） */
static inline osal_mutex_t osal_mutex_create(void)
{
    /* 返回非 NULL 占位指针，使懒创建分支 s_mutex == NULL 判断通过 */
    return (osal_mutex_t)1;
}

/** @brief 加锁（mock：永远成功） */
static inline bool osal_mutex_lock(osal_mutex_t mutex, uint32_t timeout_ms)
{
    (void)mutex;
    (void)timeout_ms;
    return true;
}

/** @brief 解锁（mock：空操作） */
static inline void osal_mutex_unlock(osal_mutex_t mutex)
{
    (void)mutex;
}

/** @brief 删除互斥锁（mock：空操作） */
static inline void osal_mutex_delete(osal_mutex_t mutex)
{
    (void)mutex;
}

#ifdef __cplusplus
}
#endif
#endif /* MOCK_OSAL_MUTEX_H */
