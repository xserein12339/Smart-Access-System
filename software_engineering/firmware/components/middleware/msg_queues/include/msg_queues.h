/**
 * @file    msg_queues.h
 * @brief   IPC 消息队列统一管理 — 数据类型定义 + 队列句柄声明
 *
 * @details 系统中所有模块间通信的消息类型和队列在此统一定义。
 *          数据流队列用于生产-消费模型（帧/坐标/特征），
 *          控制流队列用于命令下发和帧归还。
 *
 * @author  xiamu
 * @version 2.0
 */

#ifndef MSG_QUEUES_H
#define MSG_QUEUES_H

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "dal_err.h"
#include "dal_touch_interface.h"  /* dal_touch_point_t / dal_touch_event_t */
#include "face_engine.h"          /* face_box_t / face_image_t / face_feature_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  数据流消息类型
 * ================================================================ */

/**
 * @brief 触摸消息 — svc_touch → svc_ui
 *
 * @details 复用 dal_touch_point_t 定义触点属性（id/x/y/event），
 *          外加传输层时间戳。坐标已由 BSP 校准至显示屏像素坐标。
 */
typedef struct touch_msg_t {
    dal_touch_point_t point;     /**< 触点数据（复用 DAL 定义） */
    uint32_t          timestamp; /**< 入队时间戳（ms） */
} touch_msg_t;

/**
 * @brief 人脸检测结果 — svc_face_detect → svc_ui
 *
 * @note face_count=0 表示本帧未检测到人脸，boxes 无效。
 *       face_box_t 由 face_engine.h 提供（含置信度 score 字段）。
 */
typedef struct detect_to_ui_t {
    uint32_t    frame_id;                   /**< 关联的原始帧 ID */
    uint8_t     face_count;                 /**< 检测到的人脸数量 */
    face_box_t  boxes[FACE_MAX_BOXES];      /**< 边界框数组 */
} detect_to_ui_t;

/**
 * @brief 人脸图像（预处理后）— svc_face_detect → svc_face_feature
 *
 * @note buffer 指向 PSRAM 中的裁剪+对齐人脸图像。
 *       consumer 处理完后须 free(buffer)。
 */
typedef struct detect_to_feature_t {
    uint8_t    *buffer;         /**< 人脸图像数据（PSRAM） */
    uint32_t    frame_id;       /**< 关联的原始帧 ID */
    uint32_t    face_hash;      /**< 人脸唯一标识（帧ID+序号哈希） */
    uint16_t    width;          /**< 图像宽（典型 112） */
    uint16_t    height;         /**< 图像高（典型 112） */
} detect_to_feature_t;

/**
 * @brief 人脸特征向量 — svc_face_feature → svc_face_identify / svc_perm_manager
 *
 * @note features 指向 PSRAM 中的 float 数组，consumer 处理完后须 free。
 */
typedef struct feature_to_perm_t {
    float      *features;       /**< 特征向量（PSRAM） */
    uint32_t    face_hash;      /**< 关联的人脸标识 */
    uint16_t    dim;            /**< 向量维度（128/256/512） */
} feature_to_perm_t;

/**
 * @brief 人脸识别结果 — svc_perm_manager → svc_ui / svc_mqtt
 */
typedef struct identify_result_t {
    uint32_t    user_id;        /**< 识别到的用户 ID，0=陌生人 */
    char        user_name[32];  /**< 用户名称 */
    float       score;          /**< 相似度分数（0.0~1.0） */
    uint32_t    timestamp;      /**< 时间戳（unix seconds） */
} identify_result_t;

/* ================================================================
 *  控制流消息类型
 * ================================================================ */

/**
 * @brief camera 控制命令 — svc_ui → svc_camera
 */
typedef enum camera_cmdtype_t {
    CAM_CMD_PAUSE   = 0,        /**< 暂停采集 */
    CAM_CMD_RESUME,             /**< 恢复采集 */
} camera_cmdtype_t;

typedef struct camera_cmdmsg_t {
    camera_cmdtype_t cmd;       /**< 命令类型 */
} camera_cmdmsg_t;

/**
 * @brief 用户权限管理命令 — svc_ui / svc_mqtt → svc_perm_manager
 */
typedef enum perm_cmdtype_t {
    PERM_CMD_ADD_USER       = 0,    /**< 注册新用户 */
    PERM_CMD_DELETE_USER    = 1,    /**< 删除用户 */
    PERM_CMD_MODIFY_USER    = 2,    /**< 修改用户信息 */
    PERM_CMD_GET_LIST       = 3,    /**< 获取用户列表 */
    PERM_CMD_GET_INFO       = 4,    /**< 获取单个用户信息 */
    PERM_CMD_IMPORT_DB      = 5,    /**< 从外部导入特征库 */
    PERM_CMD_EXPORT_DB      = 6,    /**< 导出特征库到外部 */
    PERM_CMD_RECOG_RESULT   = 7,    /**< 识别结果反馈 → UI 弹框 */
} perm_cmdtype_t;

typedef struct ui_to_perm_t {
    perm_cmdtype_t  cmd;            /**< 命令类型 */
    uint32_t        user_id;        /**< 用户 ID */
    char            user_name[32];  /**< 用户名称 */
} ui_to_perm_t;

/**
 * @brief 权限管理响应 — svc_perm_manager → svc_ui
 */
typedef struct perm_result_t {
    perm_cmdtype_t  cmd;            /**< 原始命令类型 */
    dal_err_t       result;         /**< 执行结果 */
    uint32_t        user_id;        /**< 用户 ID */
    char            user_name[32];  /**< 用户名称（识别反馈用） */
    char            msg[64];        /**< 结果描述 */
} perm_result_t;

/* ================================================================
 *  队列句柄声明
 * ================================================================ */

/* ----- 数据流队列 ----- */
extern QueueHandle_t g_q_touch_to_ui;            /**< touch 坐标 → ui */
extern QueueHandle_t g_q_det_to_ui;              /**< detect 人脸框 → ui */
extern QueueHandle_t g_q_det_to_feature;         /**< detect 人脸图像 → feature */
extern QueueHandle_t g_q_feature_to_perm;        /**< feature 特征向量 → perm_manager */

/* ----- 控制流队列 ----- */
extern QueueHandle_t g_q_ui_to_cam;              /**< ui 控制命令 → camera */
extern QueueHandle_t g_q_ui_to_perm;             /**< ui 管理命令 → perm_manager */
extern QueueHandle_t g_q_perm_to_ui;             /**< perm 响应 → ui */

/* ================================================================
 *  API
 * ================================================================ */

/**
 * @brief 初始化所有消息队列
 * @return DAL_OK 成功，DAL_ERR_NO_MEM 队列创建失败
 */
dal_err_t msg_queue_init(void);

/**
 * @brief 销毁所有消息队列
 * @return DAL_OK
 */
dal_err_t msg_queue_deinit(void);

#ifdef __cplusplus
}
#endif
#endif /* MSG_QUEUES_H */
