/**
 * @file    dal_pir.h
 * @brief   人体红外感应设备 DAL 管理 API（注册 / 查询）
 *
 * @details 仅 BSP 初始化代码与 main(Assembler) 可包含本文件。
 *          Service 层禁止包含本文件。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef DAL_PIR_H
#define DAL_PIR_H

#include "dal_pir_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册一个 PIR 设备实例
 * @param[in] name 业务语义名称（如 "main_pir"）
 * @param[in] ops  操作契约
 * @param[in] ctx  硬件上下文（BSP 私有）
 * @return DAL_OK 成功，DAL_ERR_STATE 名称冲突，DAL_ERR_NO_MEM 注册表满
 */
dal_err_t dal_pir_register(const char *name,
                           const dal_pir_ops_t *ops,
                           void *ctx);

/**
 * @brief 按名称原子取出 PIR 设备的 ops 与 ctx
 * @param[in]  name 业务语义名称
 * @param[out] ops  返回操作契约指针
 * @param[out] ctx  返回硬件上下文指针
 * @return DAL_OK 成功，DAL_ERR_NOT_FOUND 未注册
 */
dal_err_t dal_pir_get(const char *name,
                      const dal_pir_ops_t **ops,
                      void **ctx);

#ifdef __cplusplus
}
#endif
#endif /* DAL_PIR_H */
