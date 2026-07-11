/**
 * @file    svc_camera.c
 * @brief   摄像头采集服务 — PIR 触发，计数式共享 framebuffer 供 UI/detect 消费
 *
 * @details 数据流（无帧数据队列，零拷贝共享）：
 *          PIR GPIO 双沿 ISR → xTaskNotifyFromISR(edge, s_worker)
 *          → camera task 三态机驱动采集
 *
 *          capture_frame → 发布到共享槽（refs=1，seq 递增）→ 更新 latest
 *          UI/detect 轮询 svc_camera_acquire_frame（ref++）→ 用完 release（ref--）
 *          → refs 归零则 return_frame 回收 BSP V4L2 缓冲
 *
 *          控制命令：g_q_ui_to_cam (CAM_CMD_PAUSE / CAM_CMD_RESUME)
 *
 *          状态机（state 仅任务上下文修改，ISR 仅发通知）：
 *          SUSPEND ──(PIR↑ / RESUME cmd)──▶ RUNNING
 *          RUNNING ──(PIR↓)───────────────▶ LEAVE_CONFIRM
 *          LEAVE_CONFIRM ──(timeout)───────▶ SUSPEND
 *          LEAVE_CONFIRM ──(PIR↑)──────────▶ RUNNING
 *          RUNNING/LEAVE_CONFIRM ──(PAUSE cmd)──▶ SUSPEND
 *
 *          采集：RUNNING 与 LEAVE_CONFIRM 均采集（LEAVE_CONFIRM 为 PIR↓ 后
 *          的离开确认宽限，继续采集保持预览连续，防 PIR 抖动断流）；仅
 *          SUSPEND 停采。
 *
 *          无死锁：camera 只需 1 个 refs==0 空槽即可 capture；UI/detect
 *          持的是 latest 同一槽（多 ref），camera 用其余槽。消费者跟不上
 *          时按 seq 跳过中间帧，缓冲及时回收。
 *
 * @author  xiamu
 * @version 3.0
 */

#include "svc_camera.h"
#include "board_v1_config.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_attr.h"

#define SVC_CAM_TAG       "SVC_CAM"

/* ----- 共享帧槽位数（= BSP V4L2 缓冲数，每槽对应一个 BSP 缓冲）----- */
#define SHARED_SLOTS      BOARD_CAMERA_BUFFER_COUNT
#define LATEST_INVALID    0xFFFFu   /**< s_latest_idx 未发布哨兵 */

/* ----- PIR 通知值（ISR → task，eSetBits）----- */
#define CAM_NOTIFY_RISING  1u
#define CAM_NOTIFY_FALLING 2u

/* ----- 采集/分发参数 ----- */
#define SUSPEND_POLL_MS      100    /**< SUSPEND 轮询周期（低 CPU） */
#define FRAME_DIV            CONFIG_FACE_CAM_FRAME_DIV
#define LEAVE_TIMEOUT_MS     (CONFIG_FACE_CAM_LEAVE_TIMEOUT_S * 1000)

/* ================================================================
 *  本地类型
 * ================================================================ */

/** @brief 采集任务状态 */
typedef enum {
    CAM_STA_SUSPEND       = 0,   /**< 挂起，等 PIR 上升沿 */
    CAM_STA_RUNNING       = 1,   /**< 采集中 */
    CAM_STA_LEAVE_CONFIRM = 2,   /**< PIR 下降沿，等离开确认超时 */
} camera_state_t;

/** @brief 共享帧槽位 */
typedef struct {
    dal_camera_frame_t frame;    /**< DAL 帧描述（含 frame_handle） */
    uint8_t            refs;     /**< 引用计数：1(camera) + UI + detect；0=空闲 */
} shared_slot_t;

/* ================================================================
 *  静态变量
 * ================================================================ */

static svc_camera_deps_t s_deps;                        /**< 依赖注入副本 */
static TaskHandle_t      s_worker = NULL;               /**< 采集任务句柄 */
static TaskHandle_t      s_ui_task = NULL;              /**< UI 任务句柄（可空） */
static bool              s_paused = false;             /**< UI 暂停标志（ADMIN/密码屏）：PIR↑ 不唤醒 */
static bool              s_inited = false;              /**< 初始化标志 */

static shared_slot_t     s_slots[SHARED_SLOTS];         /**< 共享帧槽位表 */
static SemaphoreHandle_t s_lock = NULL;                 /**< 保护 s_slots/latest */
static volatile uint16_t s_latest_idx = LATEST_INVALID; /**< 最新帧槽位索引 */
static volatile uint32_t s_latest_seq = 0;              /**< 最新帧序号（0=未发布） */
static uint32_t          s_seq = 0;                     /**< 帧序号发生器 */
static uint32_t          s_frame_sn = 0;                /**< 采集计数（抽稀用） */

static camera_state_t    s_state = CAM_STA_SUSPEND;     ///< 采集任务状态机（仅任务上下文修改）

/* ----- 诊断计数（每秒打印一次）----- */
static uint32_t s_stat_cap_ok;      /**< capture_frame 成功数 */
static uint32_t s_stat_cap_err;     /**< capture_frame 错误数 */
static uint32_t s_stat_pub_ok;      /**< 发布到共享槽数 */
static uint32_t s_stat_drop;        /**< 丢弃数（无空槽/抽稀外的归还不计） */
static TickType_t s_stat_last_tick; /**< 上次统计打印时刻 */

/* ================================================================
 *  PIR 中断回调（ISR 上下文）
 * ================================================================ */

/**
 * @brief PIR 双沿中断回调 — ISR 上下文，仅发任务通知
 *
 * @note 遵循中断铁律：仅 xTaskNotifyFromISR，禁止日志/内存/延时。
 *       s_worker 在 start() 内先建任务再注册本回调，stop() 内先反注册再清 s_worker。
 */
static void IRAM_ATTR pir_isr_cb(dal_pir_edge_t edge, void *user)
{
    (void)user;
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    uint32_t value = (edge == DAL_PIR_EDGE_RISING) ? CAM_NOTIFY_RISING
                                                   : CAM_NOTIFY_FALLING;
    xTaskNotifyFromISR(s_worker, value, eSetBits, &higherPriorityTaskWoken);
    if (higherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/* ================================================================
 *  共享 framebuffer 发布 / acquire / release
 * ================================================================ */

/**
 * @brief 非阻塞取一帧并发布到共享槽（camera 任务调用）
 *
 * @return true 取到帧（已发布或抽稀归还）；false 无就绪帧（调用方让出 CPU）
 *
 * @note timeout=0 非阻塞 DQBUF：无帧立即返回，camera 不阻塞。
 *       发布即更新 latest，并释放上一 latest 的 camera 引用（refs--，
 *       归零 return_frame）。return_frame 在锁外调用，避免与 BSP mutex 嵌套。
 *       抽稀（FRAME_DIV）：仅每 N 帧发布 1 帧，其余立即 return_frame。
 */
static bool capture_and_publish(void)
{
    dal_camera_frame_t frame;
    dal_err_t ret = s_deps.cam_ops->capture_frame(s_deps.cam_ctx, 0, &frame);
    if (ret == DAL_ERR_TIMEOUT) {
        return false;   /* 无就绪帧，正常，调用方让出 CPU */
    }
    if (ret != DAL_OK) {
        s_stat_cap_err++;
        ESP_LOGW(SVC_CAM_TAG, "capture err: %d", ret);
        return false;
    }
    s_stat_cap_ok++;

    /* 抽稀：未命中发布帧直接归还 */
    s_frame_sn++;
    if ((s_frame_sn % FRAME_DIV) != 0) {
        s_deps.cam_ops->return_frame(s_deps.cam_ctx, &frame);
        return true;
    }

    /* 发布到共享槽：锁内登记 + 释放旧 latest cam 引用；锁外 return_frame
     *（避免与 BSP camera mutex 嵌套）。 */
    xSemaphoreTake(s_lock, portMAX_DELAY);

    bool prev_free = false;
    bool drop = false;
    dal_camera_frame_t prev_frame;
    dal_camera_frame_t drop_frame;

    int idx = -1;
    for (uint32_t i = 0; i < SHARED_SLOTS; i++) {
        if (s_slots[i].refs == 0) {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0) {
        /* 无空槽（消费者未及时释放）：丢弃此帧 */
        drop = true;
        drop_frame = frame;
    } else {
        uint16_t prev = s_latest_idx;          /* 旧 latest（发布前） */
        s_slots[idx].frame = frame;
        s_slots[idx].refs  = 1;                /* camera 自身引用 */
        s_latest_seq = ++s_seq;
        s_latest_idx = (uint16_t)idx;
        /* 释放旧 latest 的 camera 引用（消费者仍持则不归零） */
        if (prev < SHARED_SLOTS && s_slots[prev].refs > 0) {
            s_slots[prev].refs--;
            if (s_slots[prev].refs == 0) {
                prev_free = true;
                prev_frame = s_slots[prev].frame;
                memset(&s_slots[prev], 0, sizeof(s_slots[prev]));
            }
        }
    }
    xSemaphoreGive(s_lock);

    /* 锁外归还（避免与 BSP camera mutex 嵌套） */
    if (drop) {
        s_stat_drop++;
        s_deps.cam_ops->return_frame(s_deps.cam_ctx, &drop_frame);
        return true;
    }
    if (prev_free) {
        s_deps.cam_ops->return_frame(s_deps.cam_ctx, &prev_frame);
    }
    s_stat_pub_ok++;
    return true;
}

/* ================================================================
 *  控制命令与 UI 通知
 * ================================================================ */

/** @brief 非阻塞排空控制命令队列 */
static void drain_commands(void)
{
    camera_cmdmsg_t cmd;
    while (xQueueReceive(g_q_ui_to_cam, &cmd, 0) == pdTRUE) {
        switch (cmd.cmd) {
        case CAM_CMD_PAUSE:
            s_paused = true;   /* UI 暂停（ADMIN/密码屏）：PIR↑ 不再唤醒 */
            if (s_state != CAM_STA_SUSPEND) {
                s_state = CAM_STA_SUSPEND;
                ESP_LOGI(SVC_CAM_TAG, "PAUSE → SUSPEND");
            }
            break;
        case CAM_CMD_RESUME:
            s_paused = false;
            if (s_state == CAM_STA_SUSPEND) {
                s_state = CAM_STA_RUNNING;
                ESP_LOGI(SVC_CAM_TAG, "RESUME → RUNNING");
            }
            break;
        default:
            break;
        }
    }
}

/** @brief 通知 UI 状态切换（PIR↑→预览，超时/Pause→待机） */
static void notify_ui(uint32_t value)
{
    if (s_ui_task != NULL) {
        xTaskNotify(s_ui_task, value, eSetBits);
    }
}

/* ================================================================
 *  采集任务
 * ================================================================ */

/**
 * @brief camera 采集任务 — 三态机 + 共享帧发布
 *
 * @note 状态迁移只在任务上下文发生；ISR 仅以 notify 注入边沿事件。
 */
static void svc_camera_task(void *arg)
{
    (void)arg;
    TickType_t leave_enter_tick = 0;
    s_stat_last_tick = xTaskGetTickCount();

    ESP_LOGI(SVC_CAM_TAG, "worker started, state=SUSPEND");

    while (1) {
        /* 1. 排空控制命令（PAUSE/RESUME） */
        drain_commands();

        /* 2. 取 PIR 边沿通知（按状态决定阻塞时长） */
        uint32_t wait_ms;
        if (s_state == CAM_STA_RUNNING || s_state == CAM_STA_LEAVE_CONFIRM) {
            wait_ms = 0;                       /* 采集中：非阻塞 peek，采集驱动循环 */
        } else {
            wait_ms = SUSPEND_POLL_MS;         /* SUSPEND：短轮询 */
        }

        uint32_t notify_value = 0;
        bool got_notify = (xTaskNotifyWait(0, CAM_NOTIFY_RISING | CAM_NOTIFY_FALLING,
                                           &notify_value, pdMS_TO_TICKS(wait_ms)) == pdTRUE);

        /* 3. 状态机迁移 */
        if (got_notify) {
            if (notify_value & CAM_NOTIFY_RISING) {
                /* UI 暂停（ADMIN/密码屏）期间忽略 PIR，不唤醒采集 */
                if (!s_paused && s_state != CAM_STA_RUNNING) {
                    s_state = CAM_STA_RUNNING;
                    ESP_LOGI(SVC_CAM_TAG, "PIR↑ → RUNNING");
                    notify_ui(CAMERA_NOTIFY_UI_PREVIEW);
                }
            }
            if (notify_value & CAM_NOTIFY_FALLING) {
                if (s_state == CAM_STA_RUNNING) {
                    s_state = CAM_STA_LEAVE_CONFIRM;
                    leave_enter_tick = xTaskGetTickCount();
                    ESP_LOGI(SVC_CAM_TAG, "PIR↓ → LEAVE_CONFIRM");
                }
            }
        }

        /* 4. LEAVE_CONFIRM 超时检查 */
        if (s_state == CAM_STA_LEAVE_CONFIRM) {
            uint32_t elapsed_ms = (uint32_t)((xTaskGetTickCount() - leave_enter_tick)
                                             * portTICK_PERIOD_MS);
            if (elapsed_ms >= LEAVE_TIMEOUT_MS) {
                s_state = CAM_STA_SUSPEND;
                ESP_LOGI(SVC_CAM_TAG, "leave timeout → SUSPEND");
                notify_ui(CAMERA_NOTIFY_UI_IDLE);
            }
        }

        /* 5. RUNNING/LEAVE_CONFIRM：非阻塞取帧发布；无帧则让出 CPU。
         *    LEAVE_CONFIRM 是 PIR↓ 后的离开确认宽限，期间继续采集保持预览
         *    连续（防 PIR 抖动断流），仅宽限超时（→SUSPEND）才停采。 */
        if (s_state == CAM_STA_RUNNING || s_state == CAM_STA_LEAVE_CONFIRM) {
            if (!capture_and_publish()) {
                vTaskDelay(1);   /* 无就绪帧，让出 1 tick 给 UI/detect */
            }
        }
    }
}

/* ================================================================
 *  公共 API
 * ================================================================ */

dal_err_t svc_camera_init(const svc_camera_deps_t *deps)
{
    if (s_inited) {
        return DAL_OK;
    }
    if (deps == NULL) {
        return DAL_ERR_INVALID;
    }

    /* 校验 cam_ops */
    if (deps->cam_ops == NULL ||
        deps->cam_ops->init == NULL ||
        deps->cam_ops->deinit == NULL ||
        deps->cam_ops->capture_frame == NULL ||
        deps->cam_ops->return_frame == NULL) {
        ESP_LOGE(SVC_CAM_TAG, "init: cam_ops invalid");
        return DAL_ERR_INVALID;
    }

    /* 校验 pir_ops */
    if (deps->pir_ops == NULL ||
        deps->pir_ops->init == NULL ||
        deps->pir_ops->deinit == NULL ||
        deps->pir_ops->get_state == NULL ||
        deps->pir_ops->set_edge_cb == NULL) {
        ESP_LOGE(SVC_CAM_TAG, "init: pir_ops invalid");
        return DAL_ERR_INVALID;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        ESP_LOGE(SVC_CAM_TAG, "init: mutex create failed");
        return DAL_ERR_NO_MEM;
    }

    s_deps = *deps;
    memset(s_slots, 0, sizeof(s_slots));
    s_latest_idx = LATEST_INVALID;
    s_latest_seq = 0;
    s_seq = 0;
    s_frame_sn = 0;
    s_state = CAM_STA_SUSPEND;
    s_ui_task = NULL;
    s_paused = false;

    s_inited = true;
    ESP_LOGI(SVC_CAM_TAG, "init ok");
    return DAL_OK;
}

dal_err_t svc_camera_start(void)
{
    if (!s_inited) {
        return DAL_ERR_STATE;
    }
    if (s_worker != NULL) {
        return DAL_OK;
    }

    /* -- 同步初始化摄像头 -- */
    const dal_camera_config_t cam_cfg = {
        .fb_count = BOARD_CAMERA_BUFFER_COUNT,
        .format   = DAL_CAMERA_FMT_RGB888,
        .width    = BOARD_CAMERA_FRAME_WIDTH,
        .height   = BOARD_CAMERA_FRAME_HEIGHT,
        .hflip    = false,
        .vflip    = false,
    };
    dal_err_t ret = s_deps.cam_ops->init(s_deps.cam_ctx, &cam_cfg);
    if (ret != DAL_OK) {
        ESP_LOGE(SVC_CAM_TAG, "cam init failed: %d", ret);
        return ret;
    }

    /* -- 同步初始化 PIR -- */
    ret = s_deps.pir_ops->init(s_deps.pir_ctx);
    if (ret != DAL_OK) {
        ESP_LOGE(SVC_CAM_TAG, "pir init failed: %d", ret);
        s_deps.cam_ops->deinit(s_deps.cam_ctx);
        return ret;
    }

    /* -- 建任务（s_worker 生效，pir_isr_cb 引用方可用） -- */
    BaseType_t xret = xTaskCreatePinnedToCore(
        svc_camera_task, "svc_camera",
        CONFIG_FACE_CAM_TASK_STACK, NULL,
        CONFIG_FACE_CAM_TASK_PRIO, &s_worker,
        0);
    if (xret != pdPASS) {
        s_worker = NULL;
        s_deps.pir_ops->deinit(s_deps.pir_ctx);
        s_deps.cam_ops->deinit(s_deps.cam_ctx);
        return DAL_ERR_NO_MEM;
    }

    /* -- 注册 PIR 双沿中断 -- */
    ret = s_deps.pir_ops->set_edge_cb(s_deps.pir_ctx, pir_isr_cb, NULL);
    if (ret != DAL_OK) {
        ESP_LOGE(SVC_CAM_TAG, "pir set_edge_cb failed: %d", ret);
        TaskHandle_t h = s_worker;
        s_worker = NULL;
        vTaskDelete(h);
        s_deps.pir_ops->deinit(s_deps.pir_ctx);
        s_deps.cam_ops->deinit(s_deps.cam_ctx);
        return ret;
    }

    ESP_LOGI(SVC_CAM_TAG, "task created");
    return DAL_OK;
}

dal_err_t svc_camera_stop(void)
{
    if (s_worker == NULL) {
        return DAL_OK;
    }

    ESP_LOGI(SVC_CAM_TAG, "stopping...");

    /* 1. 反注册 PIR 回调（禁用 GPIO 中断） */
    if (s_deps.pir_ops != NULL && s_deps.pir_ops->set_edge_cb != NULL) {
        s_deps.pir_ops->set_edge_cb(s_deps.pir_ctx, NULL, NULL);
    }

    /* 2. 删除任务 */
    TaskHandle_t h = s_worker;
    s_worker = NULL;
    vTaskDelete(h);

    /* 3. 回收所有共享槽（强制归还 BSP 缓冲；teardown 不等消费者） */
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (uint32_t i = 0; i < SHARED_SLOTS; i++) {
            if (s_slots[i].refs != 0) {
                s_deps.cam_ops->return_frame(s_deps.cam_ctx, &s_slots[i].frame);
                s_slots[i].refs = 0;
            }
        }
        s_latest_idx = LATEST_INVALID;
        s_latest_seq = 0;
        xSemaphoreGive(s_lock);
    }

    /* 4. 反初始化硬件 */
    if (s_deps.cam_ops != NULL && s_deps.cam_ops->deinit != NULL) {
        s_deps.cam_ops->deinit(s_deps.cam_ctx);
    }
    if (s_deps.pir_ops != NULL && s_deps.pir_ops->deinit != NULL) {
        s_deps.pir_ops->deinit(s_deps.pir_ctx);
    }

    s_inited = false;
    memset(&s_deps, 0, sizeof(s_deps));
    ESP_LOGI(SVC_CAM_TAG, "stopped");
    return DAL_OK;
}

void svc_camera_set_ui_task(TaskHandle_t ui_task)
{
    s_ui_task = ui_task;
}

bool svc_camera_acquire_frame(svc_frame_t *out, uint32_t *last_seq)
{
    if (out == NULL || last_seq == NULL || s_lock == NULL) {
        return false;
    }

    bool got = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_latest_seq != *last_seq && s_latest_idx < SHARED_SLOTS) {
        shared_slot_t *s = &s_slots[s_latest_idx];
        if (s->refs != 0) {
            s->refs++;
            out->buf    = (uint8_t *)s->frame.buf;
            out->width  = s->frame.width;
            out->height = s->frame.height;
            out->seq    = s_latest_seq;
            out->slot   = s_latest_idx;
            *last_seq   = s_latest_seq;
            got = true;
        }
    }
    xSemaphoreGive(s_lock);
    return got;
}

void svc_camera_release_frame(const svc_frame_t *f)
{
    if (f == NULL || f->slot >= SHARED_SLOTS || s_lock == NULL) {
        return;
    }

    bool free_now = false;
    dal_camera_frame_t frame_to_return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    shared_slot_t *s = &s_slots[f->slot];
    if (s->refs > 0) {
        s->refs--;
        if (s->refs == 0) {
            free_now = true;
            frame_to_return = s->frame;
            memset(s, 0, sizeof(*s));
        }
    }
    xSemaphoreGive(s_lock);

    /* 锁外归还，避免与 BSP camera mutex 嵌套 */
    if (free_now) {
        dal_err_t rret = s_deps.cam_ops->return_frame(s_deps.cam_ctx, &frame_to_return);
        if (rret != DAL_OK) {
            /* BSP 缓冲未归还（mutex 竞争/QBUF 失败）→ V4L2 缓冲泄漏，记日志 */
            ESP_LOGW(SVC_CAM_TAG, "return_frame failed ret=%d (buffer leaked)", rret);
        }
    }
}
