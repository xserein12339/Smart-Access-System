/**
 * @file    buffer_pool_manager.h
 * @brief   计数缓冲池管理 — 基于引用计数的零拷贝缓冲区复用机制
 *
 * @details 在静态预分配内存区上管理一组定长缓冲块，通过引用计数支持
 *          同一数据被多消费者（UI、人脸检测）共享，最后释放者自动回收。
 *          全静态预分配，零动态内存；alloc/free O(1)。
 *
 *  典型场景（摄像帧流水线）：
 *    camera 生产: buf = buffer_pool_alloc(&pool, timeout);  // ref=1
 *                 填充数据，发给 UI 与人脸检测
 *    UI 消费:     buffer_pool_ref(buf);  // ref=2 ... 处理 ... unref
 *    face 消费:   buffer_pool_ref(buf);  // ref=3 ... 处理 ... unref
 *    camera:      buffer_pool_unref(buf); // ref=0 -> 回收
 *
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef BUFFER_POOL_MANAGER_H
#define BUFFER_POOL_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 错误码 ---- */
#define BP_OK            0
#define BP_ERR_PARAM    -1
#define BP_ERR_STATE    -2
#define BP_ERR_NO_MEM   -3
#define BP_ERR_TIMEOUT  -4
#define BP_ERR_FULL     -5

/**
 * @brief 永久等待近似值（ms）。
 * @note xSemaphoreTake 用 pdMS_TO_TICKS，传 portMAX_DELAY
 *       (0xFFFFFFFF) 会溢出为小值，故用 1 小时作永久等待近似。
 */
#define BP_WAIT_FOREVER  (3600u * 1000u)

typedef struct buffer_pool buffer_pool_t;

/** 缓冲块（帧）描述符，生产者与消费者间传递的句柄 */
typedef struct buffer_pool_buf {
    uint8_t *buffer;            /**< 数据起始指针 */
    uint32_t size;              /**< 单块容量（字节） */
    volatile uint32_t ref;      /**< 引用计数，0 表示空闲 */
    uint16_t index;             /**< 在池中的槽位索引 */
    buffer_pool_t *pool;        /**< 所属池回指 */
} buffer_pool_buf_t;

/** 计数缓冲池管理器 */
struct buffer_pool {
    buffer_pool_buf_t *slots;
    uint8_t *storage;
    uint16_t capacity;
    uint32_t blockSize;
    uint16_t align;
    uint16_t *freeStack;
    volatile uint16_t freeTop;
    volatile bool inited;
    SemaphoreHandle_t lock;
    SemaphoreHandle_t freeSem;
};

/**
 * @brief 静态声明一个计数缓冲池及其配套存储
 *
 * 展开 4 个静态对象：<name> 池、<name>_storage 数据区、<name>_slots 槽位、
 * <name>_freestk 空闲栈。
 *
 * @param name        池实例名
 * @param block_size  单块字节数
 * @param capacity    槽位数
 * @param align       块对齐字节数（4/32 等）
 */
#define BUFFER_POOL_DEFINE(name, block_size, capacity, align)                       \
    static uint8_t name##_storage[(block_size) * (capacity)]                        \
        __attribute__((aligned(align)));                                            \
    static buffer_pool_buf_t name##_slots[capacity];                                \
    static uint16_t name##_freestk[capacity];                                       \
    static buffer_pool_t name

/**
 * @brief 初始化计数缓冲池
 * @return BP_OK 成功，BP_ERR_PARAM 参数非法，BP_ERR_NO_MEM 同步原语创建失败
 * @note 非线程安全，仅启动阶段调用一次。
 */
int buffer_pool_init(buffer_pool_t *pool,
                     uint8_t *storage,
                     uint32_t block_size,
                     uint16_t capacity,
                     buffer_pool_buf_t *slots,
                     uint16_t *free_stack,
                     uint16_t align);

/** @brief 反初始化（销毁同步原语） */
int buffer_pool_deinit(buffer_pool_t *pool);

/**
 * @brief 分配一个空闲缓冲块（池空时按 timeout 阻塞等待）
 * @return 缓冲块指针；NULL 表示超时或参数错误。分配即持有，ref=1。
 * @note 线程安全，可多任务并发。
 */
buffer_pool_buf_t *buffer_pool_alloc(buffer_pool_t *pool, uint32_t timeout_ms);

/** @brief 增加引用计数（消费者接管前调用） */
uint32_t buffer_pool_ref(buffer_pool_buf_t *buf);

/** @brief 减少引用计数，归零则自动回收 */
uint32_t buffer_pool_unref(buffer_pool_buf_t *buf);

/** @brief 查询当前空闲块数（诊断快照） */
uint16_t buffer_pool_available(const buffer_pool_t *pool);

/** @brief 查询池总容量 */
uint16_t buffer_pool_capacity(const buffer_pool_t *pool);

#ifdef __cplusplus
}
#endif
#endif /* BUFFER_POOL_MANAGER_H */
