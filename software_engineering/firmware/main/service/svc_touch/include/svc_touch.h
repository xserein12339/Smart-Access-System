/**
 * @file    svc_touch.h
 * @brief   触摸采集服务 — 独立轮询 FT5406，通过队列向 UI 发送坐标
 *
 * @details 本服务维护独立的 FreeRTOS 任务，每 20ms 轮询
 *          DAL touch_ops->read()，将 dal_touch_point_t 翻译为
 *          touch_msg_t 并通过 g_q_touch_to_ui 发送给 UI 服务。
 *
 *          FT5406 无中断引脚，纯轮询模型。20ms 周期匹配
 *          50Hz 触摸刷新率，与 LVGL 默认 33ms tick 对齐。
 *
 * @author  xiamu
 * @version 1.0
 */

#ifndef SVC_TOUCH_H
#define SVC_TOUCH_H

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"
#include "dal_err.h"
#include "dal_touch_interface.h"
#include "msg_queues.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 触摸服务依赖契约
 */
typedef struct svc_touch_deps_t {
    dal_touch_ops_t *ops;       /**< 触摸设备操作接口 */
    void            *ctx;       /**< 触摸设备私有上下文 */
} svc_touch_deps_t;

/**
 * @brief 触摸服务初始化 — 注入并校验依赖
 *
 * @param[in] deps 依赖契约指针
 * @return DAL_OK 成功，DAL_ERR_INVALID 参数非法，DAL_ERR_STATE 重复 init
 */
dal_err_t svc_touch_init(const svc_touch_deps_t *deps);

/**
 * @brief 触摸服务启动 — 初始化硬件 + 创建轮询任务
 *
 * @return DAL_OK 成功，DAL_ERR_STATE 未 init，DAL_ERR_NO_MEM 任务创建失败
 */
dal_err_t svc_touch_start(void);

/**
 * @brief 触摸服务停止 — 删除任务 + 反初始化硬件
 *
 * @return DAL_OK
 */
dal_err_t svc_touch_stop(void);

#ifdef __cplusplus
}
#endif
#endif /* SVC_TOUCH_H */
