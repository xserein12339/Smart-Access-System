/**
 * @file    dal_display.h
 * @brief   显示设备 DAL 管理 API（注册 / 查询）
 *
 * @details 仅 BSP 初始化代码与 main(Assembler) 可包含本文件。
 *          Service 层禁止包含本文件，应使用 dal_display_interface.h
 *          + Assembler 注入的 ops/ctx 指针。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef DAL_DISPLAY_H
#define DAL_DISPLAY_H

#include "dal_display_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册一个显示设备实例
 * @param[in] name 业务语义名称（如 "main_lcd"）
 * @param[in] ops  操作契约
 * @param[in] ctx  硬件上下文（BSP 私有）
 * @return DAL_OK 成功，DAL_ERR_STATE 名称冲突，DAL_ERR_NO_MEM 注册表满
 */
dal_err_t dal_display_register(const char *name,
                               const dal_display_ops_t *ops,
                               void *ctx);

/**
 * @brief 按名称原子取出显示设备的 ops 与 ctx
 * @param[in]  name 业务语义名称
 * @param[out] ops  返回操作契约指针
 * @param[out] ctx  返回硬件上下文指针
 * @return DAL_OK 成功，DAL_ERR_NOT_FOUND 未注册
 */
dal_err_t dal_display_get(const char *name,
                          const dal_display_ops_t **ops,
                          void **ctx);

#ifdef __cplusplus
}
#endif
#endif /* DAL_DISPLAY_H */
