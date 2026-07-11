/**
 * @file    dal_relay_interface.h
 * @brief   继电器 DAL 接口契约（纯业务语义，无硬件依赖）
 *
 * @details 此文件是继电器模块的纯业务语义接口规范。
 *          - Service 层、Mock 测试、BSP 适配层均应仅包含此文件。
 *          - 严禁在此文件中包含任何平台头文件（如 driver/i2c.h、pal_gpio.h）或
 *            DAL 内部管理 API（dal_relay.h）。
 *          - 接口返回值统一为 dal_err_t，底层硬件错误码由 BSP 在
 *            ops 边界翻译，不得透传。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */
#ifndef DAL_RELAY_INTERFACE_H
#define DAL_RELAY_INTERFACE_H

#include "dal_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 继电器操作接口契约
 *
 * 所有继电器后端（GPIO / I2C 扩展器 / Mock）必须实现此接口。
 * ctx 由注册方（BSP）提供，接口方法通过 ctx 访问具体硬件上下文，
 * DAL 与 Service 层不解析 ctx 内容。
 *
 * @note ops 方法默认运行在任务上下文，禁止在 ISR 中直接调用。
 */
typedef struct dal_relay_ops {
    /**
     * @brief 初始化继电器硬件（配置 GPIO 输出 + 置安全默认态）
     * @param[in] ctx 驱动上下文（BSP 私有）
     * @return DAL_OK 成功，DAL_ERR_HW 底层硬件错误
     *
     * @note 由上层（Assembler/Service）按需触发，create() 不驱动硬件。
     */
    dal_err_t (*init)(void *ctx);

    /**
     * @brief 设置继电器开关状态
     * @param[in] ctx  驱动上下文（BSP 私有）
     * @param[in] on   true=吸合, false=断开
     * @return DAL_OK 成功，DAL_ERR_HW 底层硬件错误，其余见 dal_err_t
     */
    dal_err_t (*set)(void *ctx, bool on);

    /**
     * @brief 读取继电器当前状态
     * @param[in]  ctx  驱动上下文（BSP 私有）
     * @param[out] out  输出状态：true=吸合, false=断开
     * @return DAL_OK 成功，DAL_ERR_INVALID 参数非法，DAL_ERR_HW 底层错误
     */
    dal_err_t (*get)(void *ctx, bool *out);

    /** @brief 反初始化 */
    dal_err_t (*deinit)(void *ctx);

    void *ctx;              /**< BSP 私有上下文，由 create() 注入 */
} dal_relay_ops_t;

/**
 * @brief 便捷宏：安全调用继电器 set
 * @note   避免每次手动判空，减少 Service 层样板代码
 *
 * @param ops  操作契约指针
 * @param ctx  驱动上下文
 * @param on   开关状态
 * @return DAL_OK 成功，DAL_ERR_UNSUPPORTED ops/set 为空
 */
#define DAL_RELAY_SET(ops, ctx, on) \
    ((ops) && (ops)->set ? (ops)->set((ctx), (on)) : DAL_ERR_UNSUPPORTED)

/**
 * @brief 便捷宏：安全调用继电器 get
 *
 * @param ops  操作契约指针
 * @param ctx  驱动上下文
 * @param out  状态输出指针
 * @return DAL_OK 成功，DAL_ERR_UNSUPPORTED ops/get 为空
 */
#define DAL_RELAY_GET(ops, ctx, out) \
    ((ops) && (ops)->get ? (ops)->get((ctx), (out)) : DAL_ERR_UNSUPPORTED)

#ifdef __cplusplus
}
#endif
#endif /* DAL_RELAY_INTERFACE_H */
