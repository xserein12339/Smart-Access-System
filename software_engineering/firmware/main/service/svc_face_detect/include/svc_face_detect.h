/**
 * @file    svc_face_detect.h
 * @brief   人脸检测服务 — 消费摄像头帧，输出人脸框 + 裁剪图像
 *
 * @details 本服务维护独立 FreeRTOS 任务：
 *          1. svc_camera_acquire_frame 取共享采集帧 (svc_frame_t, ref++)
 *          2. 调用 face_engine_detect() 检测人脸
 *          3. 检测结果发送 g_q_det_to_ui (人脸框，UI 叠加)
 *          4. 对每个人脸裁剪原图区域 → g_q_det_to_feature (给特征提取)
 *          5. svc_camera_release_frame 归还帧引用 (ref--)
 *
 *          不直接依赖 DAL 接口，通过 face_engine 抽象层调用算法。
 *
 * @author  xiamu
 * @version 1.0
 */

#ifndef SVC_FACE_DETECT_H
#define SVC_FACE_DETECT_H

#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 人脸检测服务初始化
 *
 * @return DAL_OK 成功, DAL_ERR_STATE 重复 init
 *
 * @note 调用前须确保 face_engine_set_ops() 已完成（模型加载 + 引擎注入）。
 */
dal_err_t svc_face_detect_init(void);

/**
 * @brief 人脸检测服务启动 — 创建检测任务
 *
 * @return DAL_OK 成功, DAL_ERR_STATE 未 init, DAL_ERR_NO_MEM 任务创建失败
 */
dal_err_t svc_face_detect_start(void);

/**
 * @brief 人脸检测服务停止 — 删除任务
 *
 * @return DAL_OK
 */
dal_err_t svc_face_detect_stop(void);

#ifdef __cplusplus
}
#endif
#endif /* SVC_FACE_DETECT_H */
