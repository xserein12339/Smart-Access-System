/**
 * @file    pal_log.h
 * @brief   PAL 日志模块 — 基于 ESP-IDF esp_log 的封装
 *
 * 提供编译期可裁剪的四级日志宏（ERROR / WARN / INFO / DEBUG），
 * 以及运行时按标签控制日志级别的函数。
 *
 * 参考文档：ESP-IDF Logging Library
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef PAL_LOG_H
#define PAL_LOG_H

#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 日志级别枚举，数值与 esp_log_level_t 兼容 */
typedef enum {
    PAL_LOG_LEVEL_NONE  = 0,   /**< 关闭全部日志 */
    PAL_LOG_LEVEL_ERROR = 1,   /**< 仅输出错误 */
    PAL_LOG_LEVEL_WARN  = 2,   /**< 错误 + 警告 */
    PAL_LOG_LEVEL_INFO  = 3,   /**< 错误 + 警告 + 信息 */
    PAL_LOG_LEVEL_DEBUG = 4,   /**< 全部日志 */
} pal_log_level_t;

/** @brief 错误日志宏，输出文件名、行号 */
#define PAL_LOGE(tag, fmt, ...)  ESP_LOGE(tag, fmt, ##__VA_ARGS__)

/** @brief 警告日志宏 */
#define PAL_LOGW(tag, fmt, ...)  ESP_LOGW(tag, fmt, ##__VA_ARGS__)

/** @brief 信息日志宏 */
#define PAL_LOGI(tag, fmt, ...)  ESP_LOGI(tag, fmt, ##__VA_ARGS__)

/** @brief 调试日志宏，仅 CONFIG_LOG_DEFAULT_LEVEL_DEBUG 下编译 */
#define PAL_LOGD(tag, fmt, ...)  ESP_LOGD(tag, fmt, ##__VA_ARGS__)

/**
 * @brief 运行时设置指定标签的最低日志级别
 *
 * @param tag   日志标签字符串（建议使用编译期常量字符串）
 * @param level 最低输出级别
 */
void pal_log_set_level(const char *tag, pal_log_level_t level);

#ifdef __cplusplus
}
#endif

#endif /* PAL_LOG_H */
