/**
 * @file    bsp_audio_es8311.h
 * @brief   ES8311 音频 BSP - 公共接口
 *
 * @note    此文件仅声明本板音频子系统的初始化入口。
 *          - 不包含引脚定义、I2C 地址、I2S 配置、寄存器细节
 *          - 不包含 dal_audio_ops_t 或硬件上下文结构体
 *          - Service 层不应包含此文件
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef BSP_AUDIO_ES8311_H
#define BSP_AUDIO_ES8311_H

#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 ES8311 音频子系统并自注册到 DAL
 *
 * @details 完成：
 *          1. 从 board_v1 获取共享 I2C 总线，初始化 ES8311 codec
 *          2. 初始化 PAL I2S（参数取自 bsp_config.h），使能 PA
 *          3. 以名称 "main_audio" 自注册到 DAL audio 模块
 *
 * @return DAL_OK 成功
 * @retval DAL_ERR_HW     底层硬件错误
 * @retval DAL_ERR_STATE  共享 I2C 未就绪 / DAL 注册冲突
 *
 * @note 依赖共享 I2C 总线已由 board_v1 初始化。
 *       注册后 Service 通过 dal_audio_get("main_audio", ...) 获取 ops。
 */
dal_err_t bsp_audio_es8311_init(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_AUDIO_ES8311_H */
