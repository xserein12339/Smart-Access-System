/**
 * @file    main.c
 * @brief   Assembler：板级初始化 + 事件总线 + 摄像采集服务
 *
 * @details 系统启动组装流程：
 *          1. board_v1_init() → 各 BSP init 自注册硬件到 DAL
 *          2. mw_event_bus_init() → 启动事件总线（各 Service init 前）
 *          3. service_camera_init → 摄像服务（不 start）
 *          4. service_ui_init/start → UI 服务 + 注册帧队列到 camera UI 通道
 *          5. service_camera_start → 启动采集（UI 已就绪接收预览）
 *          6. 其余 Service（待后续开发）
 *
 *          本文件仅负责启动期组装，业务逻辑在各 svc_xxx 中实现。
 *
 * @author  xLumina
 * @version 1.0
 */
#include <stdio.h>
#include "osal_task.h"
#include "board_v1.h"
#include "pal_log.h"

#include "mw_event_bus.h"     /* 中间件事件总线 */
#include "dal_camera.h"       /* DAL 管理 API（仅 Assembler 用） */
#include "dal_display.h"
#include "dal_touch.h"
#include "service_camera.h"   /* 摄像头服务 */
#include "service_ui.h"       /* UI 显示服务 */

void app_main(void)
{
    /* ====== Step 1: BSP 自注册到 DAL（内部完成硬件初始化 + dal_register）====== */
    board_v1_init();

    /* ====== Step 2: 启动事件总线（必须在各 Service init 前，否则订阅失败）====== */
    if (mw_event_bus_init() != DAL_OK) {
        PAL_LOGE("app_main", "event bus init failed");
        return;
    }

    /* ====== Step 3: 摄像头服务（采集流水线数据源头）====== */
    const dal_camera_ops_t *cam_ops = NULL;
    void                   *cam_ctx = NULL;
    if (dal_camera_get("main_cam", &cam_ops, &cam_ctx) != DAL_OK) {
        PAL_LOGE("app_main", "main_cam not registered");
        return;
    }
    service_camera_config_t cam_cfg = {
        .camera_ops      = cam_ops,
        .camera_ctx      = cam_ctx,
        .pool_capacity   = 4,
        .queue_depth     = 2,
        .frame_bytes     = 800u * 800u * 2u,   /* RGB565，按板级帧尺寸 */
        .alloc_timeout_ms= 1000,
    };
    if (service_camera_init(&cam_cfg) != DAL_OK) {
        PAL_LOGE("app_main", "service_camera_init failed");
        return;
    }

    /* ====== Step 4: UI 显示服务（取 display/touch 接口 + 注册帧队列）====== */
    const dal_display_ops_t *disp_ops = NULL;
    void                    *disp_ctx = NULL;
    const dal_touch_ops_t   *touch_ops = NULL;
    void                    *touch_ctx = NULL;
    if (dal_display_get("rpi7pin", &disp_ops, &disp_ctx) != DAL_OK) {
        PAL_LOGE("app_main", "display not registered");
        return;
    }
    if (dal_touch_get("main_touch", &touch_ops, &touch_ctx) != DAL_OK) {
        PAL_LOGW("app_main", "touch not registered, UI without touch");
    }
    service_ui_config_t ui_cfg = {
        .display_ops = disp_ops,
        .display_ctx = disp_ctx,
        .touch_ops   = touch_ops,
        .touch_ctx   = touch_ctx,
        .preview_w   = 480,
        .preview_h   = 480,
    };
    if (service_ui_init(&ui_cfg) != DAL_OK) {
        PAL_LOGE("app_main", "service_ui_init failed");
        return;
    }
    /* 将 UI 帧队列注册到 service_camera 的 UI 通道（须在 start 前） */
    service_camera_subscribe(SERVICE_CAMERA_CH_UI, service_ui_get_frame_queue());

    if (service_ui_start() != DAL_OK) {
        PAL_LOGE("app_main", "service_ui_start failed");
        return;
    }

    /* ====== Step 5: 启动摄像采集（UI 已就绪接收预览帧）====== */
    if (service_camera_start() != DAL_OK) {
        PAL_LOGE("app_main", "service_camera_start failed");
        return;
    }

    PAL_LOGI("app_main", "system assembled, camera streaming");

    /* ====== Step 6: 其余 Service（待后续开发）======
     *   - svc_user_manage: dal_relay_get + 订阅 AUTH/MQTT_CMD
     *   - svc_face_detect/identify、svc_db、svc_mqtt ...
     */

    /* 启动期完成，主任务退出；各 Service 内部常驻任务接管业务 */
    while (1) {
        osal_task_delay_ms(10000);
    }
}
