/**
 * @file    face_engine.h
 * @brief   人脸算法引擎可插拔接口（检测/特征提取/比对）
 *
 * @details 本层屏蔽底层 AI 实现差异（NPU / DSP / esp-who / 软推理）。
 *          Service 层（svc_face_detect/feature_extract/identify）仅依赖本接口，
 *          通过 face_engine_set_ops() 注入具体实现。
 *
 *          本期为 stub 实现（detect 恒返回 0 人脸），保证流水线结构跑通；
 *          后续接 NPU/esp-who 时仅替换 ops，Service 零修改。
 *
 *          接口运行在任务上下文（推理可能阻塞），禁止 ISR 调用。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef FACE_ENGINE_H
#define FACE_ENGINE_H

#include "dal_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FACE_FEATURE_DIM     1024u  /**< 特征向量维度 (与模型 Gemm 输出一致) */
#define FACE_MAX_BOXES       4u     /**< 单帧最大检测人脸数 */

/** 检测到的人脸框（像素坐标，原图尺度） */
typedef struct {
    uint16_t x;        /**< 左上角 X */
    uint16_t y;        /**< 左上角 Y */
    uint16_t w;        /**< 宽 */
    uint16_t h;        /**< 高 */
    float    score;    /**< 检测置信度 0~1 */
} face_box_t;

/** 输入图像描述（纯业务，不含硬件类型） */
typedef struct {
    const void *data;        /**< 像素数据（RGB565/RGB888，由 format 约定） */
    uint16_t    width;
    uint16_t    height;
    uint8_t     format;      /**< 0=RGB565, 1=RGB888 */
} face_image_t;

/** 特征向量 */
typedef struct {
    float    vec[FACE_FEATURE_DIM];   /**< 归一化特征 */
    uint16_t dim;                     /**< 有效维度 */
} face_feature_t;

/** 引擎操作契约（具体实现注入） */
typedef struct {
    dal_err_t (*init)(void *ctx);
    dal_err_t (*deinit)(void *ctx);
    /** 检测人脸，输出框数组，返回检测到的人脸数（含 0） */
    int       (*detect)(void *ctx, const face_image_t *img,
                        face_box_t *out_boxes, uint8_t max_boxes);
    /** 对裁剪对齐后的人脸提取特征 */
    dal_err_t (*extract)(void *ctx, const face_image_t *crop,
                         face_feature_t *out_feat);
    /** 比对两特征，输出相似度分数 0~1 */
    dal_err_t (*compare)(void *ctx, const face_feature_t *a,
                         const face_feature_t *b, float *out_score);
} face_engine_ops_t;

/**
 * @brief 安装引擎实现（启动期由 main 或 svc 调用一次）
 * @return DAL_OK 成功，DAL_ERR_INVALID ops 为空，DAL_ERR_STATE 已安装
 */
dal_err_t face_engine_set_ops(const face_engine_ops_t *ops, void *ctx);

/** @brief 是否已安装引擎实现 */
bool face_engine_is_ready(void);

/** @brief 检测人脸（转发到已安装 ops），未安装返回 0 */
int face_engine_detect(const face_image_t *img,
                       face_box_t *out_boxes, uint8_t max_boxes);

/** @brief 提取特征，未安装返回 DAL_ERR_UNSUPPORTED */
dal_err_t face_engine_extract(const face_image_t *crop, face_feature_t *out_feat);

/** @brief 比对特征，未安装返回 DAL_ERR_UNSUPPORTED */
dal_err_t face_engine_compare(const face_feature_t *a,
                              const face_feature_t *b, float *out_score);

/** @brief 安装 stub 实现（detect 恒返回 0 人脸） */
dal_err_t face_engine_install_stub(void);
/** @brief 安装 esp-dl HumanFaceDetect 真实引擎 */
dal_err_t face_engine_install_dl(void);

#ifdef __cplusplus
}
#endif
#endif /* FACE_ENGINE_H */
