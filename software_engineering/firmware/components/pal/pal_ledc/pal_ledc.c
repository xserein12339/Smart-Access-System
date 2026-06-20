/**
 * @file    pal_ledc.c
 * @brief   PAL LEDC 模块 - 实现（ESP-IDF ledc 驱动封装）
 *
 * ESP32-P4 仅支持 LEDC 低速模式，内部全部使用 LEDC_LOW_SPEED_MODE。
 */

#include "pal_ledc.h"

#include "driver/ledc.h"
#include "esp_err.h"

/** @brief 存储各定时器的占空比分辨率，用于百分比换算 */
static uint8_t g_duty_resolution[SOC_LEDC_TIMER_NUM] = {0};

/** @brief 内部统一使用的速度模式 */
#define PAL_LEDC_MODE  LEDC_LOW_SPEED_MODE

/* ================================================================
 *  定时器
 * ================================================================ */

int pal_ledc_timer_config(int timer_num, uint32_t freq_hz,
                          uint8_t duty_resolution)
{
    ledc_timer_config_t cfg = {
        .speed_mode      = PAL_LEDC_MODE,
        .timer_num       = (ledc_timer_t)timer_num,
        .duty_resolution = duty_resolution,
        .freq_hz         = freq_hz,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&cfg);
    if (ret == ESP_OK) {
        g_duty_resolution[timer_num] = duty_resolution;
    }
    return ret;
}

/* ================================================================
 *  通道
 * ================================================================ */

int pal_ledc_channel_config(int channel, int gpio, int timer_num)
{
    ledc_channel_config_t cfg = {
        .speed_mode = PAL_LEDC_MODE,
        .channel    = (ledc_channel_t)channel,
        .timer_sel  = (ledc_timer_t)timer_num,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = gpio,
        .duty       = 0,
        .hpoint     = 0,
        .flags      = { .output_invert = 0 },
    };
    return ledc_channel_config(&cfg);
}

/* ================================================================
 *  占空比
 * ================================================================ */

int pal_ledc_set_duty(int channel, uint32_t duty)
{
    esp_err_t ret = ledc_set_duty(PAL_LEDC_MODE, (ledc_channel_t)channel, duty);
    if (ret != ESP_OK) {
        return ret;
    }
    return ledc_update_duty(PAL_LEDC_MODE, (ledc_channel_t)channel);
}

int pal_ledc_set_duty_pct(int channel, uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }

    /* 使用默认 13 bit 分辨率计算占空比原始值 */
    uint8_t resolution = g_duty_resolution[channel];
    if (resolution == 0) {
        resolution = 13; /* 默认 13 bit */
    }

    uint32_t max_duty = ((uint32_t)1 << resolution) - 1;
    uint32_t duty = (uint32_t)((uint64_t)max_duty * percent / 100);

    return pal_ledc_set_duty(channel, duty);
}

int pal_ledc_update_duty(int channel)
{
    return ledc_update_duty(PAL_LEDC_MODE, (ledc_channel_t)channel);
}

/* ================================================================
 *  渐变
 * ================================================================ */

int pal_ledc_fade_start(int channel, uint32_t target_duty,
                         uint32_t time_ms)
{
    /* 安装渐变功能（多次调用无害） */
    esp_err_t ret = ledc_fade_func_install(0);
    if (ret != ESP_OK) {
        return ret;
    }

    return ledc_set_fade_time_and_start(PAL_LEDC_MODE,
                                         (ledc_channel_t)channel,
                                         target_duty,
                                         time_ms,
                                         LEDC_FADE_NO_WAIT);
}

int pal_ledc_fade_stop(int channel)
{
    return ledc_fade_stop(PAL_LEDC_MODE, (ledc_channel_t)channel);
}

/* ================================================================
 *  停止 / 复位
 * ================================================================ */

int pal_ledc_channel_stop(int channel, int level)
{
    return ledc_stop(PAL_LEDC_MODE, (ledc_channel_t)channel, (uint32_t)level);
}

int pal_ledc_timer_reset(int timer_num)
{
    return ledc_timer_rst(PAL_LEDC_MODE, (ledc_timer_t)timer_num);
}
