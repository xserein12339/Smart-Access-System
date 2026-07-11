/**
 * @file    bsp_camera_ov5647.h
 * @brief   OV5647 摄像头 BSP - 公共接口（create + ops 模式）
 *
 * @details 此文件声明本板摄像头子系统的 create 入口与板级调试工具。
 *          - create() 仅绑定 ops + ctx，零硬件副作用；硬件初始化在
 *            ops->init(ctx, cfg)，由上层(main/Assembler)显式触发。
 *          - 不依赖 pal_cam / pal_i2c；V4L2/esp_video 逻辑内联于 .c。
 *          - 不 include DAL 管理头（dal_camera.h），仅 include 接口契约
 *            dal_camera_interface.h。
 *          - Service 层不应包含此文件。
 *
 * @author  xLumina
 * @version 2.0
 */
#ifndef BSP_CAMERA_OV5647_H
#define BSP_CAMERA_OV5647_H

#include "dal_err.h"
#include "dal_camera_interface.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建 OV5647 摄像头 ops（仅绑定，不初始化硬件）
 *
 * @details 完成：
 *          1. memset 静态 ctx，设 fd=-1 等非硬件字段初值
 *          2. 静态 ops 编译期已注入 ops->ctx = &s_ctx
 *          3. 返回 ops 指针，供上层注册到 DAL camera 管理模块
 *
 * @return 非 NULL：指向静态 ops（ops->ctx 已注入对应静态 ctx）
 *
 * @note 仅做 struct 字段初始化，零硬件副作用。硬件初始化（esp_video_init +
 *       V4L2 全套）在 ops->init(ctx, cfg) 中完成，由上层显式触发。
 *       注册由板级组装器调用 dal_camera_register(name, ops, ops->ctx) 完成。
 *       依赖共享 I2C 总线已由 board_v1 初始化。
 */
dal_camera_ops_t *bsp_camera_ov5647_create(void);

/**
 * @brief 零拷贝：注入外部 USERPTR 缓冲（须在 init 前、create 后调用）
 *
 * ISP 输出直接写入外部缓冲（如 display fb），实现零拷贝显示。调用后 init 内部
 * 自动使用 V4L2 USERPTR 模式，缓冲由调用方管理生命周期（不分配、不释放）。
 *
 * @param[in] bufs     外部缓冲指针数组（至少 count 个，每个 buf_size 字节，DMA 对齐）
 * @param[in] count    缓冲数量（≥2，与 V4L2 队列深度匹配）
 * @param[in] buf_size 单个缓冲字节数（≥ frame_size）
 */
void bsp_camera_ov5647_set_userptr_bufs(void **bufs, uint8_t count, size_t buf_size);

/**
 * @brief ISP 黑电平校正（BLC）配置（板级自有类型，供 set_blc 使用）
 */
typedef struct {
    bool     enable;             /**< true: 启用 BLC, false: 禁用 */
    bool     stretch_enable;     /**< true: 校正后将像素值拉伸至 0~255 */
    uint16_t top_left_offset;    /**< 左上通道偏置（RGGB Bayer: R） */
    uint16_t top_right_offset;   /**< 右上通道偏置（RGGB Bayer: Gr） */
    uint16_t bottom_left_offset; /**< 左下通道偏置（RGGB Bayer: Gb） */
    uint16_t bottom_right_offset;/*< 右下通道偏置（RGGB Bayer: B） */
} bsp_cam_blc_config_t;

/**
 * @brief 设置 ISP 黑电平校正（BLC）参数
 *
 * @param cfg  BLC 配置，传 NULL 禁用
 * @return DAL_OK 成功，其他值失败
 *
 * @note 应在摄像头初始化完成后调用。BLC 偏置作用于 ISP 硬件 Bayer 数据阶段，
 *       在 demosaic/CCM 之前。OV5647 RGGB Bayer 通道映射：
 *       - top_left     -> R
 *       - top_right    -> Gr
 *       - bottom_left  -> Gb
 *       - bottom_right -> B
 *
 * @warning 若 ISP pipeline 的 IPA BLC 配置有效（JSON 已编译进固件），
 *          此函数设置的参数**可能被 IPA 后续迭代覆盖**。
 *          标定完成后建议将偏置值写入 ov5647_default.json 并重新编译。
 */
dal_err_t bsp_camera_ov5647_set_blc(const bsp_cam_blc_config_t *cfg);

/* ================================================================
 *  ISP 运行时调优接口（V4L2 ioctl 覆盖，保留自动控制器）
 *
 * @note 自动控制器(ipa)会按增益/亮度自适应 BF/Gamma/CCM/Sharpen，
 *       运行时 ioctl 对这些模块为一次性覆盖，控制器可能在增益/亮度
 *       变化时重新调整。BLC 不被自适应，覆盖稳定。稳定调优 BF/Gamma
 *       等建议结合 ov5647_default.json。NULL 或 enable=false 即禁用该模块。
 * ================================================================ */

#define BSP_CAM_ISP_MATRIX_DIM 3   /**< 3x3 矩阵维度（BF/CCM/Sharpen） */

/** BF 降噪配置（ESP32-P4 level 范围 [2,20]，matrix 值 [0,15]） */
typedef struct {
    bool    enable;     /**< true: 启用 */
    uint8_t level;      /**< 降噪等级 [2,20] */
    uint8_t matrix[BSP_CAM_ISP_MATRIX_DIM][BSP_CAM_ISP_MATRIX_DIM]; /**< 3x3 模板，值 [0,15] */
} bsp_cam_bf_config_t;

/** CCM 色彩矩阵配置（matrix float 值 (-4,4)） */
typedef struct {
    bool  enable;       /**< true: 启用 */
    float matrix[BSP_CAM_ISP_MATRIX_DIM][BSP_CAM_ISP_MATRIX_DIM]; /**< 3x3 色彩矩阵 */
} bsp_cam_ccm_config_t;

#define BSP_CAM_ISP_GAMMA_POINTS 16  /**< Gamma 曲线点数 */

/** Gamma 曲线配置（相邻 x 差值须为 2 的 N 次幂，统一作用于 RGB 三通道） */
typedef struct {
    bool    enable;     /**< true: 启用 */
    struct { uint8_t x; uint8_t y; } points[BSP_CAM_ISP_GAMMA_POINTS]; /**< 16 个曲线点 */
} bsp_cam_gamma_config_t;

/** 白平衡增益配置（red_gain/blue_gain，green 固定 1.0） */
typedef struct {
    bool  enable;       /**< true: 启用 */
    float red_gain;     /**< 红通道增益 */
    float blue_gain;    /**< 蓝通道增益 */
} bsp_cam_wb_config_t;

dal_err_t bsp_camera_ov5647_set_bf(const bsp_cam_bf_config_t *cfg);
dal_err_t bsp_camera_ov5647_set_ccm(const bsp_cam_ccm_config_t *cfg);
dal_err_t bsp_camera_ov5647_set_gamma(const bsp_cam_gamma_config_t *cfg);
dal_err_t bsp_camera_ov5647_set_wb(const bsp_cam_wb_config_t *cfg);

/**
 * @brief BLC 暗帧标定 —— 设备端采集 RAW Bayer 帧并计算四通道均值
 *
 * @details **注意：此操作会暂停当前视频流约 0.5~2 秒，完成后自动恢复。**
 *          内部流程：
 *          1. 暂停视频流
 *          2. 切换 V4L2 格式为 Bayer RAW8 (SRGGB8)
 *          3. 采集一帧
 *          4. 按 RGGB Bayer 排列分离 R/Gr/Gb/B 四通道
 *          5. 计算各通道全局均值，四舍五入取整
 *          6. 恢复原 V4L2 格式和视频流
 *          7. ESP_LOGI 打印四个 BLC 偏置值
 *          8. 若 apply=true 则自动调用 bsp_camera_ov5647_set_blc() 应用结果
 *
 * @param apply  true: 采集后自动设置 ISP BLC；false: 仅打印偏置值
 * @return DAL_OK 成功，其他值失败
 *
 * @note 调用前必须：1) 物理遮光（镜头完全封死）；2) 增益和曝光设为最小。
 *       打印的偏置值可直接填入 ov5647_default.json 的 acc.blc.blc_table。
 */
dal_err_t bsp_camera_ov5647_blc_calibrate(bool apply);

/**
 * @brief 串口 hex dump 完整 RAW Bayer 帧（供 PC 端 Python 脚本离线分析）
 *
 * @details 将一帧 RAW8 Bayer 数据通过 ESP_LOGI 以 hex 格式分块输出。
 *          输出格式：
 *          === RAW_START <width> <height> RGGB ===
 *          <hex bytes, 每行 64 字节>
 *          === RAW_END <total_bytes> ===
 *
 *          接收端（PC）使用 minicom -C capture.txt 捕获，然后运行：
 *          python3 tools/blc_calibrate.py capture_stripped.raw --width W --height H
 *
 * @return DAL_OK 成功，其他值失败
 * @note 800x800 RAW8 ~= 640KB，hex 编码后约 1.3MB，115200bps 约需 2 分钟。
 */
dal_err_t bsp_camera_ov5647_raw_dump(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_CAMERA_OV5647_H */
