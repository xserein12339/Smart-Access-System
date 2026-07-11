/**
 * @file    dal_touch_interface.h
 * @brief   触摸设备 DAL 接口契约（纯业务语义，无硬件依赖）
 *
 * @details 此文件是触摸模块的纯业务语义接口规范。
 *          - Service 层、Mock 测试、BSP 适配层均应仅包含此文件。
 *          - 严禁包含任何平台头文件或 DAL 管理头（dal_touch.h）。
 *          - 事件模型：纯轮询。Service 按需调 read() 读取当前触摸点，
 *            BSP 内部通过 I2C 读取触摸控制器状态并解析多点坐标。
 *            本板 FT5406 无中断引脚，故不提供中断/回调模型。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef DAL_TOUCH_INTERFACE_H
#define DAL_TOUCH_INTERFACE_H

#include "dal_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 触摸事件类型 */
typedef enum {
    DAL_TOUCH_EVENT_DOWN = 0,   /**< 按下 */
    DAL_TOUCH_EVENT_MOVE = 1,   /**< 移动 */
    DAL_TOUCH_EVENT_UP   = 2,   /**< 抬起 */
} dal_touch_event_t;

/** 触摸点描述（纯业务） */
typedef struct {
    uint8_t           id;       /**< 触点 ID */
    uint16_t          x;        /**< X 坐标（像素） */
    uint16_t          y;        /**< Y 坐标（像素） */
    dal_touch_event_t event;    /**< 事件类型 */
} dal_touch_point_t;

/** 触摸配置（纯业务语义） */
typedef struct {
    uint16_t h_res;   /**< 水平分辨率（用于坐标校准） */
    uint16_t v_res;   /**< 垂直分辨率 */
} dal_touch_config_t;

/** 触摸操作契约 */
typedef struct {
    /**
     * @brief 初始化触摸设备
     * @param[in] ctx 驱动上下文
     * @param[in] cfg 配置
     * @return DAL_OK 成功
     */
    dal_err_t (*init)(void *ctx, const dal_touch_config_t *cfg);

    /**
     * @brief 同步轮询读取当前触摸点
     *
     * @param[in]  ctx        驱动上下文
     * @param[out] points     输出触摸点数组（调用方提供）
     * @param[in]  max_count  points 数组容量
     * @param[out] out_count  实际读取的点数（0 = 当前无触摸）
     * @return DAL_OK 成功（含 out_count=0 无触摸情况）
     * @retval DAL_ERR_INVALID 参数非法
     * @retval DAL_ERR_HW      I2C 读取失败
     *
     * @note 轮询模型：Service 按固定间隔（如 10~20ms）调用本函数。
     */
    dal_err_t (*read)(void *ctx, dal_touch_point_t *points,
                      uint8_t max_count, uint8_t *out_count);

    /** @brief 反初始化 */
    dal_err_t (*deinit)(void *ctx);

    void *ctx;              /**< BSP 私有上下文，由 create() 注入 */
} dal_touch_ops_t;

#ifdef __cplusplus
}
#endif
#endif /* DAL_TOUCH_INTERFACE_H */
