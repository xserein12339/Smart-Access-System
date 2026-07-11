/**
 * @file    log_sink.h
 * @brief   异步日志中间件 — 生产者非阻塞 push，消费者批量落盘
 *
 * @details 采用生产者-消费者模型：业务层调用 log_sink_push() 将日志写入
 *          RAM 环形缓冲（非阻塞，满则丢弃并计数），由内部消费任务批量
 *          刷出（UART 经 esp_log，SD 卡可后续扩展）。避免 SD/Flash 写抖动
 *          阻塞识别主流程。
 *
 *          日志级别经 LOG_LEVEL 编译期裁剪（ERROR/WARN/INFO/DEBUG）。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef LOG_SINK_H
#define LOG_SINK_H

#include <stdint.h>
#include <stddef.h>
#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 日志级别 */
typedef enum {
    LOG_LVL_ERROR = 0,
    LOG_LVL_WARN  = 1,
    LOG_LVL_INFO  = 2,
    LOG_LVL_DEBUG = 3,
} log_sink_level_t;

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LVL_INFO   /**< 编译期最低输出级别 */
#endif

#define LOG_SINK_LINE_LEN  128u   /**< 单条日志最大长度（含 '\0'） */

/**
 * @brief 初始化日志下沉（创建环形缓冲 + 消费任务）
 * @return DAL_OK 成功，DAL_ERR_STATE 已初始化，DAL_ERR_NO_MEM 资源失败
 */
dal_err_t log_sink_init(void);

/**
 * @brief 非阻塞投递一条日志（格式化后入环形缓冲）
 * @note 满则丢弃并递增丢弃计数；可在任务上下文调用，ISR 禁止。
 */
void log_sink_push(log_sink_level_t level, const char *module,
                   const char *fmt, ...);

/** @brief 丢弃的日志条数（诊断） */
uint32_t log_sink_dropped_count(void);

#ifdef __cplusplus
}
#endif
#endif /* LOG_SINK_H */
