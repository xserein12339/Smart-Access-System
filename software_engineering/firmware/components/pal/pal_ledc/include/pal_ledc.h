/**
 * @file    pal_ledc.h
 * @brief   PAL LEDC 模块 — LED PWM 控制与硬件渐变
 *
 * 封装 ESP-IDF driver/ledc.h，提供定时器配置、通道绑定、
 * 占空比设置（原始值 / 百分比）以及硬件渐变接口。
 * 典型用途：LCD 背光调光、状态指示灯呼吸效果。
 *
 * @note   ESP32-P4 仅支持低速模式，API 不暴露速度模式参数。
 *
 * 参考文档：ESP32-P4 TRM LEDC 章节
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef PAL_LEDC_H
#define PAL_LEDC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  定时器配置 API
 * ================================================================ */

/**
 * @brief 配置 LEDC 定时器
 *
 * @param timer_num       定时器编号（0 ~ SOC_LEDC_TIMER_NUM - 1）
 * @param freq_hz         PWM 频率（Hz）
 * @param duty_resolution 占空比分辨率（bit），决定占空比范围 0 ~ (2^bits - 1)
 * @return 0 成功，负数失败
 */
int pal_ledc_timer_config(int timer_num, uint32_t freq_hz,
                          uint8_t duty_resolution);

/* ================================================================
 *  通道配置 API
 * ================================================================ */

/**
 * @brief 配置 LEDC 通道并绑定到 GPIO 和定时器
 *
 * @param channel   通道编号（0 ~ SOC_LEDC_CHANNEL_NUM - 1）
 * @param gpio      输出 GPIO 引脚号
 * @param timer_num 绑定的定时器编号
 * @return 0 成功，负数失败
 */
int pal_ledc_channel_config(int channel, int gpio, int timer_num);

/* ================================================================
 *  占空比控制 API
 * ================================================================ */

/**
 * @brief 设置占空比（原始计数值），立即生效
 *
 * @param channel 通道编号
 * @param duty    占空比原始值（范围取决于定时器分辨率）
 * @return 0 成功
 */
int pal_ledc_set_duty(int channel, uint32_t duty);

/**
 * @brief 设置占空比（百分比 0 ~ 100）
 *
 * @param channel 通道编号
 * @param percent 占空比百分比（0 = 关，100 = 全亮）
 * @return 0 成功
 *
 * @note 内部根据定时器分辨率（默认 13 bit）转换为原始值。
 */
int pal_ledc_set_duty_pct(int channel, uint8_t percent);

/**
 * @brief 更新占空比（需预先调用 ledc_set_duty，本函数触发硬件生效）
 *
 * @param channel 通道编号
 * @return 0 成功
 *
 * @note 通常在 ledc_set_duty 后直接调用以原子更新。
 */
int pal_ledc_update_duty(int channel);

/* ================================================================
 *  渐变 API
 * ================================================================ */

/**
 * @brief 启动硬件渐变（fade）
 *
 * 在 time_ms 时间内将占空比从当前值线性变化到 target_duty。
 * 渐变过程完全由 LEDC 硬件完成，不占用 CPU。
 *
 * @param channel     通道编号
 * @param target_duty 目标占空比原始值
 * @param time_ms     渐变时长（ms）
 * @return 0 成功
 */
int pal_ledc_fade_start(int channel, uint32_t target_duty,
                         uint32_t time_ms);

/**
 * @brief 停止正在进行的渐变并保持当前占空比
 *
 * @param channel 通道编号
 * @return 0 成功
 */
int pal_ledc_fade_stop(int channel);

/* ================================================================
 *  停止 API
 * ================================================================ */

/**
 * @brief 停止通道输出
 *
 * @param channel 通道编号
 * @param level   停止后引脚保持的电平（0 = 低，1 = 高）
 * @return 0 成功
 */
int pal_ledc_channel_stop(int channel, int level);

/**
 * @brief 重置定时器（恢复默认状态）
 *
 * @param timer_num 定时器编号
 * @return 0 成功
 */
int pal_ledc_timer_reset(int timer_num);

#ifdef __cplusplus
}
#endif

#endif /* PAL_LEDC_H */
