/**
 * @file    bsp_camera_ov5647.h
 * @brief   OV5647 摄像头 BSP - 公共接口
 *
 * @note    此文件仅声明本板摄像头子系统的初始化入口。
 *          - 不包含引脚定义、I2C 配置、帧缓冲细节
 *          - 不包含 dal_camera_ops_t 或硬件上下文结构体
 *          - Service 层不应包含此文件
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef BSP_CAMERA_OV5647_H
#define BSP_CAMERA_OV5647_H

#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 OV5647 摄像头并自注册到 DAL
 *
 * @details 完成：
 *          1. 从 board_v1 获取共享 I2C 总线（SCCB）
 *          2. 调 PAL cam 初始化 esp_video/CSI（参数取自 bsp_config.h）
 *          3. 以名称 "main_cam" 自注册到 DAL camera 模块
 *
 * @return DAL_OK 成功
 * @retval DAL_ERR_HW     底层硬件错误
 * @retval DAL_ERR_STATE  共享 I2C 未就绪 / DAL 注册冲突
 * @retval DAL_ERR_NO_MEM 资源不足
 *
 * @note 依赖共享 I2C 总线已由 board_v1 初始化。系统启动早期调用，仅一次。
 *       注册后 Service 通过 dal_camera_get("main_cam", ...) 获取 ops，
 *       再 start_stream 注册帧回调。
 */
dal_err_t bsp_camera_ov5647_init(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_CAMERA_OV5647_H */
