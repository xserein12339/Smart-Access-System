/**
 * @file    svc_wdt.c
 * @brief   系统稳定性保障服务实现 — 心跳监控 + 喂狗 + 故障告警
 *
 * @details 数据流：各业务 worker 循环内发 SERVICE_EVT_HEARTBEAT(source, timestamp) →
 *          本服务 evt_bus 回调更新 s_last_seen[source] → worker 每秒扫描，超
 *          FACE_HB_TIMEOUT_S 的 source 发 SERVICE_EVT_FAULT(code)。
 *          本任务自身 esp_task_wdt_add(NULL) + 循环 reset 喂软件看门狗。
 *
 * @author  xiamu
 * @version 1.0
 */
#include "svc_wdt.h"

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

#include "mw_event_bus.h"
#include "service_event.h"
#include "log_sink.h"

#define SVC_WDT_TAG "SVC_WDT"

/* 心跳来源索引上界（service_event_source_t 最大值 + 1） */
#define SVC_WDT_SRC_MAX  12u

/* 心跳时间戳表：0 表示该 source 从未上报 */
static volatile uint32_t s_last_seen[SVC_WDT_SRC_MAX];
/* 故障去抖：已告警的 source 标记，避免重复刷 FAULT */
static bool s_faulted[SVC_WDT_SRC_MAX];
static TaskHandle_t s_worker = NULL;
static bool s_inited = false;

/* ================================================================
 *  工具
 * ================================================================ */

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

/* ================================================================
 *  事件总线回调：记录心跳（非阻塞）
 * ================================================================ */

static void on_heartbeat(const service_event_t *event, void *user_data)
{
    (void)user_data;
    if (event == NULL) {
        return;
    }
    uint32_t src = event->arg0;
    uint32_t ts  = event->arg1;
    if (src >= SVC_WDT_SRC_MAX) {
        return;
    }
    /* arg1==0（未携带时间戳）时用本地 uptime */
    s_last_seen[src] = (ts != 0) ? ts : uptime_s();
    s_faulted[src] = false;   /* 收到心跳即清除告警标记 */
}

/* ================================================================
 *  worker：周期扫描超时 + 喂狗
 * ================================================================ */

static void wdt_worker(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    for (;;) {
        uint32_t now = uptime_s();
        for (uint32_t i = 1u; i < SVC_WDT_SRC_MAX; i++) {
            uint32_t last = s_last_seen[i];
            if (last == 0u) {
                continue;   /* 从未上报，跳过（未必所有 source 都启动） */
            }
            if ((now - last) >= (uint32_t)CONFIG_FACE_HB_TIMEOUT_S && !s_faulted[i]) {
                s_faulted[i] = true;
                log_sink_push(LOG_LVL_ERROR, SVC_WDT_TAG,
                              "heartbeat timeout: source=%lu", (unsigned long)i);
                service_event_t e = {
                    .type   = SERVICE_EVT_FAULT,
                    .source = SERVICE_SRC_WATCHDOG,
                    .arg0   = i,   /* 故障码=超时的 source */
                    .arg1   = 0,
                    .data   = NULL,
                };
                mw_event_bus_publish(&e);
            }
        }
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ================================================================
 *  公共 API
 * ================================================================ */

dal_err_t svc_wdt_init(void)
{
    if (s_inited) {
        return DAL_ERR_STATE;
    }

    mw_event_bus_subscribe(SERVICE_EVT_HEARTBEAT, on_heartbeat, NULL);

    xTaskCreatePinnedToCore(wdt_worker, "svc_wdt",
                            CONFIG_FACE_WDT_TASK_STACK, NULL,
                            CONFIG_FACE_WDT_TASK_PRIO, &s_worker, 0);
    if (s_worker == NULL) {
        return DAL_ERR_NO_MEM;
    }

    s_inited = true;
    log_sink_push(LOG_LVL_INFO, SVC_WDT_TAG, "init ok");
    return DAL_OK;
}
