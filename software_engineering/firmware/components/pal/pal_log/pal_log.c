/**
 * @file    pal_log.c
 * @brief   PAL 日志模块 - 实现
 *
 * 直接映射 ESP-IDF esp_log_level_set()，保持薄封装以零成本消除。
 */

#include "pal_log.h"

/**
 * @brief 运行时设置标签日志级别
 *
 * PAL 日志级别与 ESP-IDF esp_log_level_t 定义一致，
 * 直接强制类型转换即可安全传递。
 */
void pal_log_set_level(const char *tag, pal_log_level_t level)
{
    esp_log_level_set(tag, (esp_log_level_t)level);
}
