/**
 * @file    svc_perm_manager.h
 * @brief   权限管理服务 — 识别决策 + UI 命令处理
 *
 * @details 职责：
 *          1. 1:N 识别：收 g_q_feature_to_perm → db 比对 → 开门/拒绝 + 日志
 *          2. 删除/改密/查日志：UI 命令 → db 操作 → 回结果
 *          3. 云端下行指令（保留）
 *
 * @author  xiamu
 * @version 2.1
 */
#ifndef SVC_PERM_MANAGER_H
#define SVC_PERM_MANAGER_H

#include "dal_err.h"
#include "dal_relay_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const dal_relay_ops_t *door_lock_ops;
    void                  *door_lock_ctx;
    const dal_relay_ops_t *alarm_ops;
    void                  *alarm_ctx;
} svc_perm_manager_deps_t;

dal_err_t svc_perm_manager_init(const svc_perm_manager_deps_t *deps);

/** @brief 取消正在进行的注册流程（Back 按钮触发） */
void svc_perm_manager_cancel_enroll(void);

/** @brief 进入/退出注册模式（禁止识别开门） */
void svc_perm_manager_set_enrolling(bool en);

/** @brief 查询是否处于注册模式 */
bool svc_perm_manager_is_enrolling(void);

#ifdef __cplusplus
}
#endif
#endif /* SVC_PERM_MANAGER_H */
