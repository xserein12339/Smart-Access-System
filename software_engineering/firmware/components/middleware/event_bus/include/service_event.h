/**
 * @file    service_event.h
 * @brief   业务层统一事件定义（跨 Service 通信的公共契约）
 *
 * @details 本文件定义事件总线传递的事件类型、事件来源与事件结构体。
 *          所有 Service 通过 mw_event_bus 发布/订阅 service_event_t，
 *          实现控制流的解耦。数据流（帧/特征）仍走 OSAL 队列零拷贝传递。
 *
 *          设计原则：
 *          - 事件为轻量控制信号，arg0/arg1 携带小整数语义（状态码/PersonID），
 *            大数据经 data 指针传递（生命周期由发布者保证，分发同步调用故安全）。
 *          - EVT_NONE 作为「订阅全部」哨兵，便于日志/监控类订阅者。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef SERVICE_EVENT_H
#define SERVICE_EVENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "face_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 事件类型
 * @note   SERVICE_EVT_NONE 既表示「无事件」（publish 拒绝），
 *         也作为订阅「全部事件」的哨兵。
 */
typedef enum {
    SERVICE_EVT_NONE             = 0,   /**< 哨兵：无事件 / 订阅全部 */
    SERVICE_EVT_CAM_STATE        = 1,   /**< 摄像采集状态变更（arg0=状态） */
    SERVICE_EVT_RECOG_RESULT     = 2,   /**< 识别结果（data=识别结果指针） */
    SERVICE_EVT_AUTH_RESULT      = 3,   /**< 鉴权决策结果（arg0=PASS/REJECT/ERROR） */
    SERVICE_EVT_TOUCH            = 4,   /**< 触摸输入（data=触摸点指针） */
    SERVICE_EVT_MQTT_CONNECTED   = 5,   /**< MQTT 已连接 */
    SERVICE_EVT_MQTT_DISCONNECTED= 6,   /**< MQTT 断开 */
    SERVICE_EVT_CMD_DOWNLINK     = 7,   /**< 云端下行指令（data=service_downlink_cmd_t*，svc_mqtt 已做最小传输层解析） */
    SERVICE_EVT_LOG_READY        = 8,   /**< 日志就绪（批量落盘触发） */
    SERVICE_EVT_FAULT            = 9,   /**< 系统故障/告警（arg0=故障码） */
    SERVICE_EVT_HEARTBEAT        = 10,  /**< 任务心跳（arg0=source, arg1=时间戳） */
    SERVICE_EVT_CMD_ACK          = 11,  /**< 命令执行回执（data=service_cmd_ack_t*，供 svc_mqtt 上报云端） */
    SERVICE_EVT_FACE_BOXES       = 12,  /**< 人脸检测框（data=service_face_boxes_t*，供 UI 叠加显示） */
    SERVICE_EVT_CMD_OTA         = 13,  /**< OTA 启动命令（data=service_ota_cmd_t*，svc_mqtt 解析后直发 svc_ota） */
    SERVICE_EVT_OTA_PROGRESS    = 14,  /**< OTA 下载进度（arg0=percent 0~100, arg1=downloaded_bytes） */
    SERVICE_EVT_OTA_RESULT      = 15,  /**< OTA 结果（data=service_ota_result_t*） */
} service_event_type_t;

/** 事件来源（标识发布者，便于订阅者过滤与监控） */
typedef enum {
    SERVICE_SRC_NONE           = 0,
    SERVICE_SRC_CAMERA         = 1,
    SERVICE_SRC_FACE_DETECT    = 2,
    SERVICE_SRC_FEATURE_EXTRACT= 3,
    SERVICE_SRC_IDENTIFY       = 4,
    SERVICE_SRC_USER_MANAGE    = 5,
    SERVICE_SRC_UI             = 6,
    SERVICE_SRC_TOUCH          = 7,
    SERVICE_SRC_MQTT           = 8,
    SERVICE_SRC_DB             = 9,
    SERVICE_SRC_LOGGER         = 10,
    SERVICE_SRC_WATCHDOG       = 11,
} service_event_source_t;

/** 摄像采集状态（SERVICE_EVT_CAM_STATE.arg0） */
typedef enum {
    SERVICE_CAM_STATE_STOPPED      = 0,   /**< 停止/待机 */
    SERVICE_CAM_STATE_INITIALIZING = 1,   /**< 初始化中 */
    SERVICE_CAM_STATE_RUNNING      = 2,   /**< 采集中 */
    SERVICE_CAM_STATE_DEINITIALIZING=3,   /**< 去初始化中 */
} service_cam_state_t;

/** 鉴权决策结果（SERVICE_EVT_AUTH_RESULT.arg0） */
typedef enum {
    SERVICE_AUTH_PASS  = 0,   /**< 通过 */
    SERVICE_AUTH_REJECT= 1,   /**< 拒绝 */
    SERVICE_AUTH_ERROR = 2,   /**< 异常 */
} service_auth_result_t;

/**
 * @brief 鉴权上下文（SERVICE_EVT_AUTH_RESULT.data 指向）
 * @note  携带通行记录所需字段，供 svc_mqtt 上报 evt/record。arg0=result, arg1=method。
 *        发布者保证生命周期至分发回调返回（同步分发）。
 */
typedef struct {
    uint32_t person_id;   /**< 人员 ID，0=陌生人/远程 */
    uint32_t timestamp;   /**< Unix 秒，0=未同步 */
    uint32_t msg_id;      /**< 关联命令 msg_id（远程开门时），否则 0 */
} service_auth_ctx_t;

/** 识别结果（SERVICE_EVT_RECOG_RESULT.data 指向） */
typedef struct {
    uint32_t person_id;     /**< 识别出的人员 ID，0=未识别/陌生人 */
    float    confidence;    /**< 置信度 0~1 */
    float    quality;       /**< 人脸质量分 0~1 */
    bool     liveness;      /**< 活体判定 true=通过 */
} service_recog_result_t;

/** 云端下行指令类型（SERVICE_EVT_CMD_DOWNLINK.data 指向 service_downlink_cmd_t） */
typedef enum {
    SERVICE_CMD_REMOTE_OPEN = 1,   /**< 远程开门 */
    SERVICE_CMD_USER_ADD    = 2,   /**< 新增人员 */
    SERVICE_CMD_USER_DEL    = 3,   /**< 删除人员 */
    SERVICE_CMD_SYNC_DATA   = 4,   /**< 全量同步人员库 */
    SERVICE_CMD_OTA_START   = 5,   /**< OTA 固件升级（svc_mqtt 解析后直发 CMD_OTA 事件，body 为 ota JSON） */
} service_cmd_type_t;

/** 下行指令载体（svc_mqtt 做最小传输层解析后结构化，业务决策由 svc_perm_manager 完成） */
typedef struct {
    service_cmd_type_t type;
    uint32_t           person_id;  /**< 相关人员 ID */
    char               name[32];   /**< USER_ADD 时姓名 */
    uint32_t           msg_id;     /**< 全局唯一消息 ID（去重/ACK） */
    char               body[256];  /**< sync_data 等复杂命令的原始 JSON */
} service_downlink_cmd_t;

/**
 * @brief 命令执行回执（SERVICE_EVT_CMD_ACK.data 指向）
 * @note  由 svc_perm_manager 执行完命令后发布，svc_mqtt 据此上报云端 cmd_ack。
 *        msg_id 必须与对应下行命令一致，供云端关联。
 */
typedef struct {
    uint32_t msg_id;           /**< 关联的下行命令 msg_id */
    uint8_t  code;             /**< 0=ok,1=rejected,2=db_error,3=unknown,4=dup */
    char     msg[32];          /**< 可读描述 */
} service_cmd_ack_t;

/**
 * @brief OTA 命令载体（SERVICE_EVT_CMD_OTA.data 指向）
 * @note  svc_mqtt 解析 ota_start JSON 后填充，直发 svc_ota 执行。
 */
typedef struct {
    char     version[16];   /**< 固件版本号 */
    uint32_t size;          /**< 固件大小 (bytes) */
    char     sha256[65];    /**< SHA256 校验值 (64 hex + NUL) */
    char     url[192];      /**< 下载地址 */
    uint32_t msg_id;        /**< 关联 MQTT 消息 ID */
} service_ota_cmd_t;

/**
 * @brief OTA 结果载体（SERVICE_EVT_OTA_RESULT.data 指向）
 */
typedef struct {
    uint32_t msg_id;           /**< 关联命令 msg_id */
    uint8_t  code;             /**< 0=ok 1=download_failed 2=sha256_mismatch 3=write_error 4=invalid_url */
    char     msg[64];          /**< 可读描述 */
} service_ota_result_t;

/** 人脸检测框载体（SERVICE_EVT_FACE_BOXES.data 指向） */
typedef struct {
    face_box_t boxes[FACE_MAX_BOXES];  /**< 检测框数组 */
    uint8_t    count;                  /**< 实际检测数（0=无人脸） */
} service_face_boxes_t;

/** 业务事件载体 */
typedef struct {
    service_event_type_t   type;    /**< 事件类型 */
    service_event_source_t source;  /**< 发布来源 */
    uint32_t               arg0;    /**< 语义参数 0 */
    uint32_t               arg1;    /**< 语义参数 1 */
    void                  *data;    /**< 附加数据指针（发布者保证生命周期） */
} service_event_t;

#ifdef __cplusplus
}
#endif
#endif /* SERVICE_EVENT_H */
