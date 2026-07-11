/**
 * @file    log_sink.c
 * @brief   异步日志实现 — 环形缓冲 + 消费任务
 *
 * @details 单生产者/多生产者写入环形缓冲（mutex 保护写索引），消费任务
 *          阻塞等信号量后批量取出刷出。本期刷出经 esp_log（UART），
 *          SD 卡批量落盘可后续扩展（消费任务内 fopen 追加）。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "log_sink.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LS_TAG "LOG_SINK"
#define LS_BUF_DEPTH   64u
#define LS_TASK_STACK  4096
#define LS_TASK_PRIO   1    /* 最低优先级，可被所有业务抢占 */
#define LS_TASK_CORE   0
#define LS_BATCH       8u   /* 每轮最多刷出条数 */

/** 单条日志条目 */
typedef struct {
    log_sink_level_t level;
    char             module[16];
    char             text[LOG_SINK_LINE_LEN];
} log_entry_t;

static log_entry_t    s_buf[LS_BUF_DEPTH];
static volatile uint16_t s_head = 0;   /* 消费者读 */
static volatile uint16_t s_tail = 0;   /* 生产者写 */
static SemaphoreHandle_t   s_mutex;
static SemaphoreHandle_t   s_sem;
static TaskHandle_t s_task;
static volatile uint32_t s_dropped = 0;
static bool           s_inited = false;

static const char *level_str(log_sink_level_t l)
{
    switch (l) {
    case LOG_LVL_ERROR: return "E";
    case LOG_LVL_WARN:  return "W";
    case LOG_LVL_INFO:  return "I";
    case LOG_LVL_DEBUG: return "D";
    default:            return "?";
    }
}

static void flush_one(const log_entry_t *e)
{
    /* 经 esp_log 输出到 UART（异步队列已解耦业务） */
    ESP_LOGI(LS_TAG, "[%s][%s] %s", level_str(e->level), e->module, e->text);
}

static void consumer_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* 等待新日志，长超时近似永久等待 */
        if (xSemaphoreTake(s_sem, pdMS_TO_TICKS(3600u * 1000u)) != pdTRUE) {
            continue;
        }
        for (uint16_t i = 0; i < LS_BATCH; i++) {
            log_entry_t e;
            bool got = false;
            xSemaphoreTake(s_mutex, pdMS_TO_TICKS(3600u * 1000u));
            if (s_head != s_tail) {
                e = s_buf[s_head];
                s_head = (s_head + 1u) % LS_BUF_DEPTH;
                got = true;
            }
            xSemaphoreGive(s_mutex);
            if (!got) {
                break;
            }
            flush_one(&e);
        }
    }
}

dal_err_t log_sink_init(void)
{
    if (s_inited) {
        return DAL_ERR_STATE;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return DAL_ERR_NO_MEM;
    }
    s_sem = xSemaphoreCreateCounting(LS_BUF_DEPTH, 0);
    if (s_sem == NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return DAL_ERR_NO_MEM;
    }
    s_head = s_tail = 0;
    s_dropped = 0;

    /* core_id < 0 用 xTaskCreate（无亲和），否则 xTaskCreatePinnedToCore。
     * 失败时 handle 保持 NULL，与原 osal_task_create 返回 NULL 语义一致。 */
    s_task = NULL;
    if (LS_TASK_CORE < 0) {
        xTaskCreate(consumer_task, "log_sink", LS_TASK_STACK,
                    NULL, LS_TASK_PRIO, &s_task);
    } else {
        xTaskCreatePinnedToCore(consumer_task, "log_sink", LS_TASK_STACK,
                                NULL, LS_TASK_PRIO, &s_task, LS_TASK_CORE);
    }
    if (s_task == NULL) {
        vSemaphoreDelete(s_sem);
        vSemaphoreDelete(s_mutex);
        s_sem = NULL;
        s_mutex = NULL;
        return DAL_ERR_NO_MEM;
    }
    s_inited = true;
    ESP_LOGI(LS_TAG, "ready (depth=%u)", LS_BUF_DEPTH);
    return DAL_OK;
}

void log_sink_push(log_sink_level_t level, const char *module,
                   const char *fmt, ...)
{
    if (!s_inited || (int)level > (int)LOG_LEVEL) {
        return;
    }

    log_entry_t e;
    e.level = level;
    snprintf(e.module, sizeof(e.module), "%s", module ? module : "?");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e.text, sizeof(e.text), fmt ? fmt : "", ap);
    va_end(ap);

    xSemaphoreTake(s_mutex, pdMS_TO_TICKS(3600u * 1000u));
    uint16_t next = (s_tail + 1u) % LS_BUF_DEPTH;
    if (next == s_head) {
        /* 缓冲满，丢弃并计数 */
        s_dropped++;
        xSemaphoreGive(s_mutex);
        return;
    }
    s_buf[s_tail] = e;
    s_tail = next;
    xSemaphoreGive(s_mutex);

    xSemaphoreGive(s_sem);
}

uint32_t log_sink_dropped_count(void)
{
    return s_dropped;
}
