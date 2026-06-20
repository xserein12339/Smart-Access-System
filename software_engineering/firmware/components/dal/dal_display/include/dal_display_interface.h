/**
 * @file    dal_display_interface.h
 * @brief   显示设备 DAL 接口契约（纯业务语义，无硬件依赖）
 *
 * @details 此文件是显示模块的纯业务语义接口规范。
 *          - Service 层、Mock 测试、BSP 适配层均应仅包含此文件。
 *          - 严禁包含任何平台头文件（如 driver/esp_lcd.h、pal_mipi_dsi.h）或
 *            DAL 管理头（dal_display.h）。
 *          - 接口返回值统一为 dal_err_t，底层硬件错误码由 BSP 在
 *            ops 边界翻译，不得透传。
 *          - 所有硬件上下文封装在不透明指针 void *ctx 中，DAL/Service 不解析。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef DAL_DISPLAY_INTERFACE_H
#define DAL_DISPLAY_INTERFACE_H

#include "dal_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 显示设备配置（纯业务语义，无引脚/时序） */
typedef struct {
    uint16_t width;        /**< 像素宽 */
    uint16_t height;       /**< 像素高 */
    uint8_t  brightness;   /**< 背光百分比 0~100 */
} dal_display_config_t;

/** 显示设备操作契约 */
typedef struct {
    /**
     * @brief 初始化显示设备（应用运行参数）
     * @param[in] ctx 驱动上下文（BSP 私有）
     * @param[in] cfg 显示配置
     * @return DAL_OK 成功，DAL_ERR_HW 底层错误
     */
    dal_err_t (*init)(void *ctx, const dal_display_config_t *cfg);

    /**
     * @brief 用指定颜色填充矩形区域
     * @param[in] ctx   驱动上下文
     * @param[in] x,y,w,h 矩形区域
     * @param[in] color 填充色（RGB565 16 位值，0xBBGGGRRRRRGGGGGBBBBB 中 R5G6B5）
     * @return DAL_OK 成功
     */
    dal_err_t (*fill)(void *ctx, uint16_t x, uint16_t y,
                      uint16_t w, uint16_t h, uint16_t color);

    /**
     * @brief 在指定区域绘制位图
     * @param[in] ctx  驱动上下文
     * @param[in] x,y  区域左上角
     * @param[in] w,h  区域宽高
     * @param[in] data 像素数据（格式由 BSP 约定，默认 RGB565）
     * @return DAL_OK 成功
     */
    dal_err_t (*draw_bitmap)(void *ctx, uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h, const void *data);

    /**
     * @brief 设置背光亮度
     * @param[in] ctx     驱动上下文
     * @param[in] percent 背光百分比 0~100
     * @return DAL_OK 成功
     */
    dal_err_t (*set_backlight)(void *ctx, uint8_t percent);

    /**
     * @brief 获取显示帧缓冲指针（供直接绘制，可选）
     * @param[in]  ctx 驱动上下文
     * @param[out] fb  返回帧缓冲起始地址
     * @return DAL_OK 成功，DAL_ERR_UNSUPPORTED 该后端不提供直接 FB 访问
     */
    dal_err_t (*get_fb)(void *ctx, void **fb);
} dal_display_ops_t;

#ifdef __cplusplus
}
#endif
#endif /* DAL_DISPLAY_INTERFACE_H */
