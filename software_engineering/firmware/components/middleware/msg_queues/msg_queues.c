/**
 * @file    msg_queues.c
 * @brief   IPC 消息队列实现 — 创建 / 销毁
 *
 * @details 所有队列在 msg_queue_init() 中统一创建，
 *          在 msg_queue_deinit() 中统一销毁。
 *          帧数据不再走队列——camera 采集帧经计数式共享 framebuffer
 *          (svc_camera_acquire/release_frame) 供 UI/detect 零拷贝消费。
 *          本模块仅保留：触摸/检测结果等数据流队列 + 控制命令队列。
 *
 * @author  xiamu
 * @version 3.0
 */

#include "msg_queues.h"
#include "esp_log.h"

#define MQ_TAG "MSGQ"

/* ----- 数据流队列 ----- */
QueueHandle_t g_q_touch_to_ui        = NULL;
QueueHandle_t g_q_det_to_ui          = NULL;
QueueHandle_t g_q_det_to_feature     = NULL;
QueueHandle_t g_q_feature_to_perm    = NULL;

/* ----- 控制流队列 ----- */
QueueHandle_t g_q_ui_to_cam          = NULL;
QueueHandle_t g_q_ui_to_perm         = NULL;
QueueHandle_t g_q_perm_to_ui         = NULL;

dal_err_t msg_queue_init(void)
{
    /* ====== 数据流队列 ====== */
    /* touch → ui: 坐标事件，20ms 一次故深度 2 够用 */
    g_q_touch_to_ui = xQueueCreate(2, sizeof(touch_msg_t));

    /* detect → ui: 人脸框叠加，每帧 1 次 */
    g_q_det_to_ui = xQueueCreate(2, sizeof(detect_to_ui_t));

    /* detect → feature: 人脸图像传递，含 PSRAM 指针，深度适中 */
    g_q_det_to_feature = xQueueCreate(3, sizeof(detect_to_feature_t));

    /* feature → perm: 特征向量传递，深度适中 */
    g_q_feature_to_perm = xQueueCreate(8, sizeof(feature_to_perm_t));

    /* ====== 控制流队列 ====== */
    /* ui → camera: 暂停/恢复命令，深度 1 保证最新命令覆盖 */
    g_q_ui_to_cam = xQueueCreate(1, sizeof(camera_cmdmsg_t));

    /* ui → perm: 添加/删除用户命令，深度 2 */
    g_q_ui_to_perm = xQueueCreate(2, sizeof(ui_to_perm_t));

    /* perm → ui: 操作结果响应，深度 2 */
    g_q_perm_to_ui = xQueueCreate(2, sizeof(perm_result_t));

    /* ====== 校验 ====== */
    if (!g_q_touch_to_ui || !g_q_det_to_ui   || !g_q_det_to_feature ||
        !g_q_feature_to_perm ||
        !g_q_ui_to_cam   || !g_q_ui_to_perm  || !g_q_perm_to_ui) {
        ESP_LOGE(MQ_TAG, "queue create failed, cleaning up");
        msg_queue_deinit();
        return DAL_ERR_NO_MEM;
    }

    ESP_LOGI(MQ_TAG, "init ok: 4 data + 3 ctrl = 7 queues");
    return DAL_OK;
}

/** @brief 安全删除一个队列并将句柄置 NULL */
static void mq_delete(QueueHandle_t *q)
{
    if (*q != NULL) {
        vQueueDelete(*q);
        *q = NULL;
    }
}

dal_err_t msg_queue_deinit(void)
{
    mq_delete(&g_q_touch_to_ui);
    mq_delete(&g_q_det_to_ui);
    mq_delete(&g_q_det_to_feature);
    mq_delete(&g_q_feature_to_perm);

    mq_delete(&g_q_ui_to_cam);
    mq_delete(&g_q_ui_to_perm);
    mq_delete(&g_q_perm_to_ui);

    return DAL_OK;
}
