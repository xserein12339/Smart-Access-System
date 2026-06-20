/**
 * @file    pal_ldo.h
 * @brief   PAL LDO 模块 — 片内 LDO 稳压器通道管理
 *
 * 封装 ESP-IDF esp_ldo_regulator.h，提供统一的 LDO 通道申请、电压调节与释放接口。
 * ESP32-P4 内置多路 LDO（如 LDO_VO1~VO4），可用于 MIPI-CSI/DSI PHY、传感器等供电。
 * 同一通道被多次 acquire 时底层会引用计数，release 需与 acquire 一一对应。
 *
 * 参考文档：ESP32-P4 数据手册 PMU/LDO 章节
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef PAL_LDO_H
#define PAL_LDO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief LDO 通道不透明句柄 */
typedef void *pal_ldo_handle_t;

/* ================================================================
 *  配置类型
 * ================================================================ */

/**
 * @brief LDO 通道申请配置
 */
typedef struct {
    int      chan_id;    /**< LDO 通道 ID，按数据手册指定，如 LDO_VO1 设为 1 */
    uint32_t voltage_mv; /**< 目标输出电压（mV） */
} pal_ldo_config_t;

/* ================================================================
 *  生命周期 API
 * ================================================================ */

/**
 * @brief 申请 LDO 通道并设置初始电压
 *
 * @param[out] handle 返回的通道句柄
 * @param[in]  cfg    通道配置
 * @return 0 成功，负数失败
 *
 * @note 同一通道可被多次申请，底层引用计数；release 次数需匹配。
 */
int pal_ldo_acquire(pal_ldo_handle_t *handle, const pal_ldo_config_t *cfg);

/**
 * @brief 释放 LDO 通道（引用计数减 1，归零后真正关闭）
 *
 * @param handle 通道句柄，释放后置为无效
 * @return 0 成功，负数失败
 */
int pal_ldo_release(pal_ldo_handle_t handle);

/* ================================================================
 *  运行时 API
 * ================================================================ */

/**
 * @brief 运行时调节 LDO 通道输出电压
 *
 * @param handle     通道句柄
 * @param voltage_mv 目标电压（mV）
 * @return 0 成功，负数失败
 */
int pal_ldo_set_voltage(pal_ldo_handle_t handle, uint32_t voltage_mv);

/**
 * @brief 获取底层 esp_ldo_channel_handle_t（供需要直接操作底层的场景）
 *
 * @param handle PAL LDO 句柄
 * @return 底层 esp_ldo_channel_handle_t，无效句柄返回 NULL
 */
void *pal_ldo_get_handle(pal_ldo_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* PAL_LDO_H */
