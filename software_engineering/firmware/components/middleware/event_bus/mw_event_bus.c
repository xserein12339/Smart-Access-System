/**
 * @file    mw_event_bus.c
 * @brief   事件总线中间件实现
 *
 * @details 订阅表为数组（容量 MW_EVENT_BUS_MAX_SUBSCRIBERS），mutex 保护增删；
 *          事件经 FreeRTOS 队列缓冲（容量 MW_EVENT_BUS_QUEUE_DEPTH），分发任务
 *          阻塞 receive 后线性扫描订阅表，type 精确匹配或 EVT_NONE 全匹配者回调。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "mw_event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

#define MW_EVENT_BUS_TAG              "EVT_BUS"
#define MW_EVENT_BUS_MAX_SUBSCRIBERS  16
#define MW_EVENT_BUS_QUEUE_DEPTH      32
#define MW_EVENT_BUS_TASK_STACK       4096
#define MW_EVENT_BUS_TASK_PRIO        4
#define MW_EVENT_BUS_TASK_CORE        0

/** 订阅条目 */
typedef struct {
    service_event_type_t type;     /**< 订阅类型，EVT_NONE=全部 */
    mw_event_bus_cb_t    cb;       /**< 回调，NULL 表示空槽 */
    void                *user_data;
} subscriber_t;

static subscriber_t  s_subs[MW_EVENT_BUS_MAX_SUBSCRIBERS];
static SemaphoreHandle_t  s_mutex;
static QueueHandle_t  s_queue;
static TaskHandle_t s_task;
static bool          s_inited = false;

/* ================================================================
 *  分发任务
 * ================================================================ */
static void dispatch_task(void *arg)
{
    (void)arg;
    service_event_t evt;
    for (;;) {
        if (xQueueReceive(s_queue, &evt, pdMS_TO_TICKS(3600u * 1000u)) != pdTRUE) {
            continue;   /* 长超时近似永久等待，超时重试 */
        }
        /* 同步分发：扫描订阅表，匹配者回调。
         * 回调在分发任务上下文执行，故 data 指针在此期间有效。 */
        xSemaphoreTake(s_mutex, pdMS_TO_TICKS(3600u * 1000u));
        for (size_t i = 0; i < MW_EVENT_BUS_MAX_SUBSCRIBERS; i++) {
            if (s_subs[i].cb == NULL) {
                continue;
            }
            if (s_subs[i].type == SERVICE_EVT_NONE ||
                s_subs[i].type == evt.type) {
                /* 拷贝回调指针与 user_data 后释放锁，避免持锁回调导致死锁
                 * （回调内可能 publish/subscribe）。 */
                mw_event_bus_cb_t cb  = s_subs[i].cb;
                void             *ud  = s_subs[i].user_data;
                xSemaphoreGive(s_mutex);
                cb(&evt, ud);
                xSemaphoreTake(s_mutex, pdMS_TO_TICKS(3600u * 1000u));
            }
        }
        xSemaphoreGive(s_mutex);
    }
}

/* ================================================================
 *  公共 API
 * ================================================================ */
dal_err_t mw_event_bus_init(void)
{
    if (s_inited) {
        return DAL_ERR_STATE;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return DAL_ERR_NO_MEM;
    }
    /* xQueueCreate 参数顺序为 (队列长度, 单项字节数)，与原 osal_queue_create 相反 */
    s_queue = xQueueCreate(MW_EVENT_BUS_QUEUE_DEPTH, sizeof(service_event_t));
    if (s_queue == NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return DAL_ERR_NO_MEM;
    }
    memset(s_subs, 0, sizeof(s_subs));

    /* core_id < 0 用 xTaskCreate（无亲和），否则 xTaskCreatePinnedToCore。
     * 失败时 handle 保持 NULL，与原 osal_task_create 返回 NULL 语义一致。 */
    s_task = NULL;
    if (MW_EVENT_BUS_TASK_CORE < 0) {
        xTaskCreate(dispatch_task, "evt_bus", MW_EVENT_BUS_TASK_STACK,
                    NULL, MW_EVENT_BUS_TASK_PRIO, &s_task);
    } else {
        xTaskCreatePinnedToCore(dispatch_task, "evt_bus", MW_EVENT_BUS_TASK_STACK,
                                NULL, MW_EVENT_BUS_TASK_PRIO, &s_task,
                                MW_EVENT_BUS_TASK_CORE);
    }
    if (s_task == NULL) {
        vQueueDelete(s_queue);
        vSemaphoreDelete(s_mutex);
        s_queue = NULL;
        s_mutex = NULL;
        return DAL_ERR_NO_MEM;
    }

    s_inited = true;
    ESP_LOGI(MW_EVENT_BUS_TAG, "event bus ready (subs=%d qdepth=%d)",
             MW_EVENT_BUS_MAX_SUBSCRIBERS, MW_EVENT_BUS_QUEUE_DEPTH);
    return DAL_OK;
}

dal_err_t mw_event_bus_subscribe(service_event_type_t type,
                                mw_event_bus_cb_t cb,
                                void *user_data)
{
    if (!s_inited || cb == NULL) {
        return DAL_ERR_INVALID;
    }

    dal_err_t ret = DAL_ERR_NO_MEM;
    xSemaphoreTake(s_mutex, pdMS_TO_TICKS(3600u * 1000u));
    for (size_t i = 0; i < MW_EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        if (s_subs[i].cb == NULL) {
            s_subs[i].type      = type;
            s_subs[i].cb        = cb;
            s_subs[i].user_data = user_data;
            ret = DAL_OK;
            break;
        }
    }
    xSemaphoreGive(s_mutex);
    return ret;
}

dal_err_t mw_event_bus_publish(const service_event_t *event)
{
    if (!s_inited || event == NULL || event->type == SERVICE_EVT_NONE) {
        return DAL_ERR_INVALID;
    }
    /* 非阻塞投递：队列满时返回 BUSY，由发布者决定丢弃/降级 */
    if (xQueueSend(s_queue, event, pdMS_TO_TICKS(0)) != pdTRUE) {
        return DAL_ERR_BUSY;
    }
    return DAL_OK;
}
