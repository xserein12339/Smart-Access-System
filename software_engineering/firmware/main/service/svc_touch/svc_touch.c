/**
 * @file    svc_touch.c
 * @brief   触摸采集服务 — 独立轮询任务 + 队列发送
 *
 * @details 独立于 UI 服务的触摸采集任务：
 *          每 20ms 轮询 DAL touch_ops->read() → 翻译为 touch_msg_t
 *          → xQueueSend(g_q_touch_to_ui) → svc_ui 消费。
 *
 *          分离带来的好处：
 *          - 触摸和 UI 生命周期解耦（触摸可独立启停）
 *          - UI 不依赖 dal_touch_interface.h，只依赖 msg_queues.h
 *          - 未来可接入手势识别或远程触摸中间件，无需改 UI
 *
 * @author  xiamu
 * @version 1.0
 */

#include "svc_touch.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define SVC_TOUCH_TAG "SVC_TOUCH"

/* ----- 触摸分辨率（与显示一致）----- */
#define TOUCH_H_RES  800
#define TOUCH_V_RES  480

/* ----- 轮询周期（ms），匹配 50Hz 触摸刷新率 ----- */
#define TOUCH_POLL_MS  20

/* ----- 单次读取最大触点 ----- */
#define TOUCH_MAX_PTS  5

/* ================================================================
 *  静态变量
 * ================================================================ */

static svc_touch_deps_t s_deps;                     /**< 依赖注入副本 */
static TaskHandle_t     s_worker = NULL;             /**< 轮询任务句柄 */
static bool             s_inited = false;            /**< 初始化标志 */

/* ================================================================
 *  触摸轮询任务
 * ================================================================ */

static void s_touch_task(void *arg)
{
    (void)arg;

    /* 触摸硬件已由 svc_touch_start() 同步初始化，worker 仅轮询 read */

    dal_touch_point_t pts[TOUCH_MAX_PTS];
    dal_err_t ret;
    uint8_t prev_count = 0;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t log_cnt = 0;   /* 诊断：前 5 次触摸打印 */

    ESP_LOGI(SVC_TOUCH_TAG, "worker started, poll=%dms", TOUCH_POLL_MS);

    for (;;) {
        uint8_t count = 0;
        ret = s_deps.ops->read(s_deps.ctx, pts, TOUCH_MAX_PTS, &count);

        if (ret == DAL_OK) {
            /* 边沿触发：仅在按下/抬起状态变化时发事件，避免按住时每轮推 DOWN
             * 泛滥队列（depth 2）→ UI 误判释放、CLICKED 不触发。 */
            if (count > 0 && prev_count == 0) {
                /* 按下边沿：发首触点。event 用边沿判定覆盖——BSP 的 pts[0].event
                 * 依赖 FT5406 event_id 映射，可能误报（Contact 被映射成 UP），
                 * 不可靠。svc_touch 以 count 边沿为准。 */
                touch_msg_t msg = {
                    .point     = pts[0],
                    .timestamp = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                };
                msg.point.event = DAL_TOUCH_EVENT_DOWN;
                if (log_cnt < 5) {
                    log_cnt++;
                    ESP_LOGI(SVC_TOUCH_TAG, "touch[%u] DOWN: x=%u y=%u",
                             (unsigned)log_cnt, (unsigned)pts[0].x, (unsigned)pts[0].y);
                }
                xQueueSend(g_q_touch_to_ui, &msg, 0);
            } else if (count == 0 && prev_count > 0) {
                /* 抬起边沿：发 UP */
                touch_msg_t up_msg = {
                    .point     = { .event = DAL_TOUCH_EVENT_UP, .x = 0, .y = 0, .id = 0 },
                    .timestamp = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                };
                xQueueSend(g_q_touch_to_ui, &up_msg, 0);
            }
            prev_count = count;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TOUCH_POLL_MS));
    }
}

/* ================================================================
 *  公共 API
 * ================================================================ */

dal_err_t svc_touch_init(const svc_touch_deps_t *deps)
{
    if (s_inited) {
        return DAL_ERR_STATE;
    }

    if (deps == NULL || deps->ops == NULL) {
        return DAL_ERR_INVALID;
    }

    /* 校验函数指针 */
    if (deps->ops->init == NULL ||
        deps->ops->read == NULL ||
        deps->ops->deinit == NULL) {
        ESP_LOGE(SVC_TOUCH_TAG, "init: ops invalid");
        return DAL_ERR_INVALID;
    }

    s_deps   = *deps;
    s_inited = true;
    ESP_LOGI(SVC_TOUCH_TAG, "init ok");
    return DAL_OK;
}

dal_err_t svc_touch_start(void)
{
    if (!s_inited) {
        return DAL_ERR_STATE;
    }
    if (s_worker != NULL) {
        return DAL_OK;
    }

    /* -- 同步初始化触摸硬件（Assembler 不 init，由 touch 服务管理） -- */
    const dal_touch_config_t cfg = {
        .h_res = TOUCH_H_RES,
        .v_res = TOUCH_V_RES,
    };
    dal_err_t ret = s_deps.ops->init(s_deps.ctx, &cfg);
    if (ret != DAL_OK) {
        ESP_LOGE(SVC_TOUCH_TAG, "hw init failed: %d", ret);
        return ret;
    }

    BaseType_t xret = xTaskCreatePinnedToCore(
        s_touch_task, "svc_touch",
        CONFIG_FACE_TOUCH_TASK_STACK, NULL,
        CONFIG_FACE_TOUCH_TASK_PRIO, &s_worker,
        0   /* Core 0 */
    );
    if (xret != pdPASS) {
        s_worker = NULL;
        s_deps.ops->deinit(s_deps.ctx);
        return DAL_ERR_NO_MEM;
    }

    ESP_LOGI(SVC_TOUCH_TAG, "task created");
    return DAL_OK;
}

dal_err_t svc_touch_stop(void)
{
    if (s_worker == NULL) {
        return DAL_OK;
    }

    TaskHandle_t h = s_worker;
    s_worker = NULL;
    vTaskDelete(h);

    if (s_deps.ops != NULL && s_deps.ops->deinit != NULL) {
        s_deps.ops->deinit(s_deps.ctx);
    }

    ESP_LOGI(SVC_TOUCH_TAG, "stopped");
    return DAL_OK;
}
