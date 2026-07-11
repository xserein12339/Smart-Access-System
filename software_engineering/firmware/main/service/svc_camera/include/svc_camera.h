/**
 * @file    svc_camera.h
 * @brief   摄像头采集服务 — PIR 触发，共享 framebuffer 供 UI / 检测消费
 *
 * @details 本服务封装摄像头 + PIR 的联动采集逻辑：
 *          - PIR 上升沿自动激活采集，下降沿经离开确认后挂起
 *          - 采集帧发布到计数式共享 framebuffer（引用计数），UI/检测
 *            通过 acquire/release 轮询取最新帧，无帧数据队列推送
 *          - 支持 PAUSE/RESUME 控制命令
 *          - BSP/DAL 不创建任务，采集任务由本服务维护
 *
 * @author  xiamu
 * @version 3.0
 */

#ifndef SVC_CAMERA_H
#define SVC_CAMERA_H

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"
#include "dal_err.h"
#include "dal_camera_interface.h"
#include "dal_pir_interface.h"
#include "msg_queues.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief camera 服务依赖契约
 *
 * @note 由 main (Assembler) 在启动阶段注入。
 *       ops 和 ctx 生命周期须覆盖整个运行期。
 */
typedef struct svc_camera_deps_t {
    dal_camera_ops_t    *cam_ops;       /**< 摄像设备操作接口 */
    void                *cam_ctx;       /**< 摄像设备私有上下文 */
    dal_pir_ops_t       *pir_ops;       /**< 红外感应设备操作接口 */
    void                *pir_ctx;       /**< 红外感应设备私有上下文 */
} svc_camera_deps_t;

/**
 * @brief camera 服务初始化 — 注入并校验依赖
 *
 * @param[in] deps 依赖契约指针
 * @return DAL_OK 成功，DAL_ERR_INVALID 参数或函数指针非法
 *
 * @note 可重入：多次调用仅首次生效。
 *       本函数仅存储依赖，不创建任务，不操作硬件。
 */
dal_err_t svc_camera_init(const svc_camera_deps_t *deps);

/**
 * @brief camera 服务启动 — 创建 FreeRTOS 采集任务
 *
 * @return DAL_OK 成功，DAL_ERR_STATE 未 init，DAL_ERR_NO_MEM 任务创建失败
 *
 * @note 可重入：任务已运行时直接返回 DAL_OK。
 *       任务初始进入 SUSPEND 状态，由 PIR 上升沿或 RESUME 命令唤醒。
 */
dal_err_t svc_camera_start(void);

/**
 * @brief camera 服务停止 — 删除任务并反初始化硬件
 *
 * @return DAL_OK 成功
 *
 * @note 停止后须重新 init + start 才能再次运行。
 *       反注册 PIR ISR → 删任务 → 回收共享帧 → deinit 硬件。
 */
dal_err_t svc_camera_stop(void);

/**
 * @brief 注册 UI 任务句柄，供 camera worker 用 xTaskNotify 推送界面切换事件
 *
 * @param[in] ui_task UI 任务句柄（NULL 表示注销）
 *
 * @note UI 服务在 svc_ui_start() 内调用本函数注册。
 *       camera 在状态切换时通知 UI：PIR↑→切 PREVIEW，超时/Pause→回 IDLE。
 */
void svc_camera_set_ui_task(TaskHandle_t ui_task);

/* ================================================================
 *  共享 framebuffer（计数式，供 UI / detect 零拷贝消费）
 * ================================================================ */

/** @brief 共享帧描述（acquire 返回，release 回传） */
typedef struct {
    uint8_t  *buf;        /**< 帧数据（BSP DMA 缓冲，零拷贝） */
    uint16_t  width;      /**< 帧宽 */
    uint16_t  height;     /**< 帧高 */
    uint32_t  seq;        /**< 帧序号 */
    uint16_t  slot;       /**< 内部槽位索引（release 用，外部勿改） */
} svc_frame_t;

/**
 * @brief 取最新帧的一个引用（ref++）
 *
 * @param[out] out      帧描述（buf/width/height/seq/slot）
 * @param[in,out] last_seq 上次消费的 seq（输入），输出本次 seq
 *
 * @return true 取到新帧（seq 变化）；false 无新帧（*last_seq 未变）
 *
 * @note 线程安全，UI/detect 可并发调用。取到的帧须配对 release_frame。
 *       消费者跟不上时自动跳过中间帧（last_seq 判定），不阻塞 camera。
 */
bool svc_camera_acquire_frame(svc_frame_t *out, uint32_t *last_seq);

/**
 * @brief 释放帧引用（ref--），归零则回收 BSP 缓冲
 *
 * @param[in] f acquire 返回的帧描述（slot 字段定位槽位）
 *
 * @note 线程安全。release 后不得再访问 f->buf。
 */
void svc_camera_release_frame(const svc_frame_t *f);

/** @brief camera→UI 通知值 */
#define CAMERA_NOTIFY_UI_PREVIEW   0x100u  /**< PIR↑：切预览 */
#define CAMERA_NOTIFY_UI_IDLE      0x200u  /**< 超时/Pause：回待机 */

#ifdef __cplusplus
}
#endif
#endif /* SVC_CAMERA_H */
