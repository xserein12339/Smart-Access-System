/**
 * @file    svc_ui.h
 * @brief   人机界面服务 — LVGL 渲染 + 摄像头预览 + 触摸消费
 *
 * @details 本服务封装 LVGL 的初始化和渲染循环。
 *          触摸输入通过 g_q_touch_to_ui 队列从 svc_touch 接收，
 *          不再直接依赖 DAL touch 接口。
 *
 *          摄像头预览：svc_camera_acquire/release_frame 共享 framebuffer → LVGL image。
 *          控制输出：g_q_ui_to_cam (PAUSE/RESUME)。
 *
 * @author  xiamu
 * @version 2.0
 */

#ifndef SVC_UI_H
#define SVC_UI_H

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"
#include "dal_err.h"
#include "dal_display_interface.h"
#include "msg_queues.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UI 服务依赖契约（仅显示设备）
 */
typedef struct svc_ui_deps_t {
    dal_display_ops_t   *disp_ops;      /**< 显示设备操作接口 */
    void                *disp_ctx;      /**< 显示设备私有上下文 */
} svc_ui_deps_t;

/**
 * @brief UI 服务初始化 — 注入并校验依赖
 *
 * @param[in] deps 依赖契约指针
 * @return DAL_OK 成功，DAL_ERR_INVALID 参数非法，DAL_ERR_STATE 重复 init
 *
 * @note 可重入：多次调用仅首次生效。仅存储依赖，不创建任务。
 */
dal_err_t svc_ui_init(const svc_ui_deps_t *deps);

/**
 * @brief UI 服务启动 — 初始化 LVGL + 显示 + 创建 UI 任务
 *
 * @return DAL_OK 成功，DAL_ERR_STATE 未 init，DAL_ERR_NO_MEM 任务创建失败
 *
 * @note 可重入：任务已运行时直接返回 DAL_OK。
 *       触摸由 svc_touch 独立管理，UI 通过 g_q_touch_to_ui 接收事件。
 */
dal_err_t svc_ui_start(void);

#ifdef __cplusplus
}
#endif
#endif /* SVC_UI_H */
