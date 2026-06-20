/**
 * @file    pal_wdt.h
 * @brief   PAL WDT 模块 — 任务看门狗（TWDT）
 *
 * 封装 ESP-IDF esp_task_wdt.h（Task Watchdog Timer）。TWDT 基于 FreeRTOS，
 * 用于监控任务是否周期性"喂狗"，超时可触发 panic 并打印触发的任务/用户。
 * 适合监控关键任务存活状态，而非硬实时的硬件看门狗复位。
 *
 * 典型用法：
 *   1. pal_wdt_init() 初始化全局 TWDT（仅一次）；
 *   2. 关键任务调用 pal_wdt_add_user() 注册自己；
 *   3. 任务循环中周期性 pal_wdt_reset_user() 喂狗；
 *   4. 退出时 pal_wdt_delete_user() 注销。
 *
 * 参考文档：ESP32-P4 TRM Timer Group 章节 / ESP-IDF TWDT 文档
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef PAL_WDT_H
#define PAL_WDT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief TWDT 用户句柄（用于非 FreeRTOS 任务的用户订阅） */
typedef void *pal_wdt_user_handle_t;

/* ================================================================
 *  全局 TWDT 配置
 * ================================================================ */

/**
 * @brief TWDT 全局初始化配置
 */
typedef struct {
    uint32_t timeout_ms;      /**< 超时时间（ms） */
    uint32_t idle_core_mask;  /**< 监控空闲任务的核掩码，1<<i 表示监控核 i 的 idle 任务 */
    bool     trigger_panic;   /**< 超时是否触发 panic（true=复位，false=仅打印） */
} pal_wdt_config_t;

/* ================================================================
 *  全局 TWDT API
 * ================================================================ */

/**
 * @brief 初始化全局 TWDT（整个系统仅需调用一次）
 *
 * @param cfg TWDT 配置
 * @return 0 成功，负数失败
 */
int pal_wdt_init(const pal_wdt_config_t *cfg);

/**
 * @brief 重新配置已初始化的 TWDT（运行时修改超时/panic 等）
 *
 * @param cfg 新配置
 * @return 0 成功，负数失败
 */
int pal_wdt_reconfigure(const pal_wdt_config_t *cfg);

/**
 * @brief 反初始化全局 TWDT
 *
 * @return 0 成功，负数失败
 */
int pal_wdt_deinit(void);

/* ================================================================
 *  用户订阅 API（推荐：不依赖 FreeRTOS 任务句柄）
 * ================================================================ */

/**
 * @brief 注册一个 TWDT 用户（需自行周期性喂狗）
 *
 * @param name   用户名（调试用，建议传常量字符串）
 * @param[out] handle 返回的用户句柄
 * @return 0 成功，负数失败
 */
int pal_wdt_add_user(const char *name, pal_wdt_user_handle_t *handle);

/**
 * @brief 为指定用户喂狗（重置其超时计时）
 *
 * @param handle 用户句柄
 * @return 0 成功，负数失败
 */
int pal_wdt_reset_user(pal_wdt_user_handle_t handle);

/**
 * @brief 注销 TWDT 用户
 *
 * @param handle 用户句柄
 * @return 0 成功，负数失败
 */
int pal_wdt_delete_user(pal_wdt_user_handle_t handle);

/* ================================================================
 *  任务订阅 API（订阅当前 FreeRTOS 任务）
 * ================================================================ */

/**
 * @brief 为当前运行的任务订阅 TWDT
 *
 * @return 0 成功，负数失败
 *
 * @note 订阅后需在任务循环中调用 pal_wdt_reset() 喂狗。
 */
int pal_wdt_add_task(void);

/**
 * @brief 为当前任务喂狗
 *
 * @return 0 成功，负数失败
 */
int pal_wdt_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PAL_WDT_H */
