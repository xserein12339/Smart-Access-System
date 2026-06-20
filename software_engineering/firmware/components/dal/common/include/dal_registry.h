/**
 * @file    dal_registry.h
 * @brief   DAL 通用设备注册表（泛型注册/查询）
 *
 * @details 各 dal_xxx 模块的「按名称注册 + 原子查询 ops/ctx」逻辑高度一致，
 *          抽出此通用注册表避免重复实现。每个 dal_xxx.c 持有一个静态
 *          dal_registry_t 实例 + 静态 entry 数组。
 *
 *          线程安全：内部用 osal_mutex 保护（懒创建，依赖 ESP-IDF 启动模型
 *          下 app_main 单线程顺序调用各 bsp_xxx_init，register 不并发；
 *          get 可能在多任务并发，此时 mutex 已就绪）。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef DAL_REGISTRY_H
#define DAL_REGISTRY_H

#include "dal_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 单个注册表条目 */
typedef struct {
    const char *name;   /**< 业务语义名称（生命周期需覆盖实例使用期） */
    const void *ops;    /**< 操作契约指针（具体类型由各模块定义） */
    void       *ctx;    /**< 驱动上下文（BSP 私有，DAL 不解析） */
} dal_registry_entry_t;

/** 注册表实例 */
typedef struct {
    dal_registry_entry_t *entries;  /**< 条目数组（由调用方提供静态存储） */
    uint8_t               max;      /**< 最大条目数 */
    uint8_t               count;    /**< 当前条目数 */
    bool                  mutex_created; /**< mutex 是否已创建 */
} dal_registry_t;

/**
 * @brief 初始化注册表（绑定静态条目数组）
 *
 * @param reg     注册表实例
 * @param entries 调用方提供的静态条目数组（max 个，清零）
 * @param max     条目数组容量
 * @return DAL_OK 成功，DAL_ERR_INVALID 参数非法
 *
 * @note 不在此创建 mutex，mutex 在首次 register 时懒创建。
 */
dal_err_t dal_registry_init(dal_registry_t *reg,
                            dal_registry_entry_t *entries,
                            uint8_t max);

/**
 * @brief 注册一个设备实例
 *
 * @param reg  注册表实例
 * @param name 业务语义名称
 * @param ops  操作契约
 * @param ctx  驱动上下文
 * @return DAL_OK 成功，DAL_ERR_INVALID/NO_MEM/STATE 失败
 */
dal_err_t dal_registry_register(dal_registry_t *reg,
                                const char *name,
                                const void *ops,
                                void *ctx);

/**
 * @brief 原子取出实例的 ops 与 ctx
 *
 * @param reg  注册表实例
 * @param name 业务语义名称
 * @param ops  输出 ops 指针（失败置 NULL）
 * @param ctx  输出 ctx 指针（失败置 NULL）
 * @return DAL_OK 成功，DAL_ERR_INVALID/NOT_FOUND 失败
 *
 * @note ops 与 ctx 在同一临界区取出，避免分两次取值期间不一致。
 */
dal_err_t dal_registry_get(dal_registry_t *reg,
                           const char *name,
                           const void **ops,
                           void **ctx);

#ifdef __cplusplus
}
#endif
#endif /* DAL_REGISTRY_H */
