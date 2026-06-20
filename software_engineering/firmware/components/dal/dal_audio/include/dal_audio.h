/**
 * @file    dal_audio.h
 * @brief   音频设备 DAL 管理 API（注册 / 查询）
 *
 * @details 仅 BSP 初始化代码与 main(Assembler) 可包含本文件。
 *          Service 层禁止包含本文件。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef DAL_AUDIO_H
#define DAL_AUDIO_H

#include "dal_audio_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册一个音频设备实例
 * @param[in] name 业务语义名称（如 "main_audio"）
 * @param[in] ops  操作契约
 * @param[in] ctx  硬件上下文（BSP 私有）
 * @return DAL_OK 成功，DAL_ERR_STATE 名称冲突，DAL_ERR_NO_MEM 注册表满
 */
dal_err_t dal_audio_register(const char *name,
                             const dal_audio_ops_t *ops,
                             void *ctx);

/**
 * @brief 按名称原子取出音频设备的 ops 与 ctx
 * @param[in]  name 业务语义名称
 * @param[out] ops  返回操作契约指针
 * @param[out] ctx  返回硬件上下文指针
 * @return DAL_OK 成功，DAL_ERR_NOT_FOUND 未注册
 */
dal_err_t dal_audio_get(const char *name,
                        const dal_audio_ops_t **ops,
                        void **ctx);

#ifdef __cplusplus
}
#endif
#endif /* DAL_AUDIO_H */
