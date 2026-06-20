/**
 * @file    dal_relay.h
 * @brief   继电器 DAL 管理 API（注册 / 查询）
 *
 * @details 仅 BSP 初始化代码与 main(Assembler) 可包含本文件。
 *          Service 层禁止包含本文件，应使用 dal_relay_interface.h
 *          + Assembler 注入的 ops/ctx 指针。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */
#ifndef DAL_RELAY_H
#define DAL_RELAY_H

#include "dal_relay_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册一个继电器实例
 *
 * @param[in] name 唯一业务语义名称（如 "door_lock"），生命周期需覆盖实例使用期
 * @param[in] ops  操作契约实现
 * @param[in] ctx  驱动上下文（BSP 私有，DAL 不解析）
 *
 * @return DAL_OK 成功
 * @retval DAL_ERR_INVALID name/ops 为空
 * @retval DAL_ERR_NO_MEM  注册表已满
 * @retval DAL_ERR_STATE   名称已存在（重复注册）
 *
 * @note 注册表内部用 OSAL 互斥锁保护，线程安全。
 *       mutex 在首次 register 时懒创建（依赖 ESP-IDF 启动模型：
 *       app_main 单线程顺序调用各 bsp_xxx_init，register 不会并发）。
 */
dal_err_t dal_relay_register(const char *name,
                             const dal_relay_ops_t *ops,
                             void *ctx);

/**
 * @brief 按名称原子取出继电器实例的 ops 与 ctx
 *
 * @param[in]  name 业务语义名称
 * @param[out] ops  返回操作契约指针（失败时置 NULL）
 * @param[out] ctx  返回驱动上下文指针（失败时置 NULL）
 *
 * @return DAL_OK 成功
 * @retval DAL_ERR_INVALID   参数非法
 * @retval DAL_ERR_NOT_FOUND 实例未注册
 *
 * @note ops 与 ctx 在同一临界区内一次性取出，避免分两次取值期间
 *       实例被注销导致的不一致。取出的指针在调用方使用期间，
 *       其生命周期由 BSP 保证（本实现不支持运行时注销）。
 */
dal_err_t dal_relay_get(const char *name,
                        const dal_relay_ops_t **ops,
                        void **ctx);

#ifdef __cplusplus
}
#endif
#endif /* DAL_RELAY_H */
