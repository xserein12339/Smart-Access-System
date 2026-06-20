/**
 * @file    buffer_pool_manager.c
 * @brief   计数缓冲池管理实现
 *
 * @details 空闲块以索引栈管理，push/pop O(1)。alloc 先 take 计数信号量
 *          （可阻塞）再 mutex 内 pop 索引，避免持锁等待；unref 归零时
 *          push 回栈并 give 信号量。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "buffer_pool_manager.h"
#include <stddef.h>

/* ---- 内部：计算 storage 内第 index 块的数据起始地址（按 align 对齐）---- */
static uint8_t *bp_slot_addr(buffer_pool_t *pool, uint16_t index)
{
    uint32_t stride = (uint32_t)pool->blockSize;
    uint16_t a = pool->align;
    if (a > 1) {
        stride = (stride + a - 1u) & ~((uint32_t)a - 1u);
    }
    return pool->storage + stride * index;
}

int buffer_pool_init(buffer_pool_t *pool,
                     uint8_t *storage,
                     uint32_t block_size,
                     uint16_t capacity,
                     buffer_pool_buf_t *slots,
                     uint16_t *free_stack,
                     uint16_t align)
{
    if (pool == NULL || storage == NULL || block_size == 0 ||
        capacity == 0 || slots == NULL || free_stack == NULL) {
        return BP_ERR_PARAM;
    }
    if (align == 0) {
        align = 1;
    }
    if (pool->inited) {
        return BP_ERR_STATE;
    }

    pool->storage    = storage;
    pool->blockSize  = block_size;
    pool->capacity   = capacity;
    pool->align      = align;
    pool->slots      = slots;
    pool->freeStack  = free_stack;
    pool->freeTop    = capacity;
    pool->inited     = false;

    /* 初始化槽位：映射数据指针、索引、所属池，引用计数归零 */
    for (uint16_t i = 0; i < capacity; i++) {
        slots[i].buffer = bp_slot_addr(pool, i);
        slots[i].size   = block_size;
        slots[i].ref    = 0;
        slots[i].index  = i;
        slots[i].pool   = pool;
        /* 空闲栈逆序填充：index 0 在栈底，capacity-1 在栈顶 */
        free_stack[capacity - 1u - i] = i;
    }

    pool->lock = osal_mutex_create();
    if (pool->lock == NULL) {
        return BP_ERR_NO_MEM;
    }
    /* 计数信号量初值 = capacity，反映空闲块数 */
    pool->freeSem = osal_sem_create_counting(capacity, capacity);
    if (pool->freeSem == NULL) {
        osal_mutex_delete(pool->lock);
        pool->lock = NULL;
        return BP_ERR_NO_MEM;
    }

    pool->inited = true;
    return BP_OK;
}

int buffer_pool_deinit(buffer_pool_t *pool)
{
    if (pool == NULL) {
        return BP_ERR_PARAM;
    }
    if (!pool->inited) {
        return BP_ERR_STATE;
    }
    osal_sem_delete(pool->freeSem);
    osal_mutex_delete(pool->lock);
    pool->freeSem = NULL;
    pool->lock    = NULL;
    pool->inited  = false;
    pool->freeTop = 0;
    return BP_OK;
}

buffer_pool_buf_t *buffer_pool_alloc(buffer_pool_t *pool, uint32_t timeout_ms)
{
    if (pool == NULL || !pool->inited) {
        return NULL;
    }

    /* 先等空闲块名额（可阻塞），再进临界区取索引，避免持锁等待 */
    if (!osal_sem_take(pool->freeSem, timeout_ms)) {
        return NULL;
    }

    buffer_pool_buf_t *buf = NULL;
    osal_mutex_lock(pool->lock, BP_WAIT_FOREVER);
    if (pool->freeTop > 0) {
        uint16_t idx = pool->freeStack[--pool->freeTop];
        buf = &pool->slots[idx];
        buf->ref = 1;
    }
    osal_mutex_unlock(pool->lock);

    if (buf == NULL) {
        /* 信号量与空闲栈计数不一致的极端情况，归还名额避免泄漏 */
        osal_sem_give(pool->freeSem);
    }
    return buf;
}

uint32_t buffer_pool_ref(buffer_pool_buf_t *buf)
{
    if (buf == NULL || buf->pool == NULL || !buf->pool->inited) {
        return 0;
    }
    uint32_t newRef;
    osal_mutex_lock(buf->pool->lock, BP_WAIT_FOREVER);
    if (buf->ref == 0) {
        buf->ref = 1;
    } else {
        buf->ref++;
    }
    newRef = buf->ref;
    osal_mutex_unlock(buf->pool->lock);
    return newRef;
}

uint32_t buffer_pool_unref(buffer_pool_buf_t *buf)
{
    if (buf == NULL || buf->pool == NULL || !buf->pool->inited) {
        return 0xFFFFFFFFu;
    }

    buffer_pool_t *pool = buf->pool;
    uint32_t newRef;

    osal_mutex_lock(pool->lock, BP_WAIT_FOREVER);
    if (buf->ref == 0) {
        /* 重复释放，维持计数自洽，不回收 */
        osal_mutex_unlock(pool->lock);
        return 0;
    }
    buf->ref--;
    newRef = buf->ref;

    if (newRef == 0) {
        if (pool->freeTop < pool->capacity) {
            pool->freeStack[pool->freeTop++] = buf->index;
        }
    }
    osal_mutex_unlock(pool->lock);

    if (newRef == 0) {
        osal_sem_give(pool->freeSem);
    }
    return newRef;
}

uint16_t buffer_pool_available(const buffer_pool_t *pool)
{
    if (pool == NULL || !pool->inited) {
        return 0;
    }
    return pool->freeTop;
}

uint16_t buffer_pool_capacity(const buffer_pool_t *pool)
{
    if (pool == NULL) {
        return 0;
    }
    return pool->capacity;
}
