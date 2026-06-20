/**
 * @file    bsp_display_tc358762.h
 * @brief   TC358762 DSI→RGB 桥接芯片子驱动（BSP 内部，不放入 include/）
 *
 * @details TC358762 寄存器通过 DSI Generic Long Write (DT=0x29) 6 字节包配置，
 *          不支持 I2C。本驱动封装 PAL mipi_dsi 主机初始化 + 桥寄存器初始化 +
 *          帧缓冲绘图操作。
 *
 *          初始化顺序（由组装器编排）：
 *            1. pal_mipi_dsi_init（DSI 主机 + DPI 面板 + 帧缓冲）
 *            2. attiny88 释放桥复位
 *            3. bsp_tc358762_config_bridge（写桥寄存器序列 + 启动 PPI/DSI）
 *
 *          仅 bsp_display_rpi7pin.c 组装器可包含本文件。
 *
 *          参考：Linux drivers/gpu/drm/bridge/tc358762.c + 树莓派 7" DSI 屏文档
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef BSP_DISPLAY_TC358762_H
#define BSP_DISPLAY_TC358762_H

#include "dal_err.h"
#include "pal_mipi_dsi.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- TC358762 寄存器地址（16-bit，经 DSI Generic Write 写入）---- */
#define TC358762_REG_DSI_LANEENABLE   0x0100
#define TC358762_REG_CLRSIPOCOUNT     0x0160
#define TC358762_REG_ATMR             0x0168
#define TC358762_REG_LPTXTIMECNT      0x0208
#define TC358762_REG_HS_HBP           0x0380
#define TC358762_REG_HDISP_HFP        0x0384
#define TC358762_REG_VS_VBP           0x0388
#define TC358762_REG_VDISP_VFP        0x038C
#define TC358762_REG_SYSCTRL          0x0460
#define TC358762_REG_LCDCTRL          0x0478
#define TC358762_REG_PPI_STARTPPI     0x0480
#define TC358762_REG_DSI_STARTDSI     0x0484

#define DSI_LANEENABLE_CLOCK          (1 << 0)
#define DSI_LANEENABLE_D0             (1 << 1)

/** TC358762 桥接芯片驱动上下文（BSP 私有） */
typedef struct {
    pal_mipi_dsi_handle_t dsi;   /**< PAL DSI 句柄 */
    uint8_t               vc;    /**< DSI 虚拟通道 */
    uint16_t              width;
    uint16_t              height;
    bool                  inited;
} bsp_tc358762_ctx_t;

/**
 * @brief 初始化 DSI 主机 + DPI 面板（不含桥寄存器）
 * @param[out] ctx 上下文
 * @return DAL_OK 成功
 * @note 引脚/时序/DSI 参数取自 bsp_config.h 宏。调用后需先释放桥复位，
 *       再调 bsp_tc358762_config_bridge() 配置桥寄存器。
 */
dal_err_t bsp_tc358762_init(bsp_tc358762_ctx_t *ctx);

/**
 * @brief 写 TC358762 桥寄存器初始化序列并启动 PPI/DSI
 *
 * @details 必须在 DSI 主机初始化、ATTINY88 释放桥复位之后调用。
 *          写入 DSI 通道使能、PPI、LCD 控制、系统控制、时序寄存器，
 *          最后启动 PPI/DSI。
 *
 * @return DAL_OK 成功
 */
dal_err_t bsp_tc358762_config_bridge(bsp_tc358762_ctx_t *ctx);

/** @brief 反初始化（停桥接 + 反初始化 DSI 主机） */
dal_err_t bsp_tc358762_deinit(bsp_tc358762_ctx_t *ctx);

/** @brief 填充矩形 */
dal_err_t bsp_tc358762_fill(bsp_tc358762_ctx_t *ctx,
                             uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h, uint16_t color);

/** @brief 绘制位图 */
dal_err_t bsp_tc358762_draw_bitmap(bsp_tc358762_ctx_t *ctx,
                                   uint16_t x, uint16_t y,
                                   uint16_t w, uint16_t h, const void *data);

/** @brief 获取帧缓冲指针 */
dal_err_t bsp_tc358762_get_fb(bsp_tc358762_ctx_t *ctx, void **fb);

#ifdef __cplusplus
}
#endif
#endif /* BSP_DISPLAY_TC358762_H */
