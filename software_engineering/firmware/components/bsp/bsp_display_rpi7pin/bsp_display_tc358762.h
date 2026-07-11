/**
 * @file    bsp_display_tc358762.h
 * @brief   TC358762 DSI→RGB 桥接芯片子驱动（BSP 内部，不放入 include/）
 *
 * @details 直接内联 ESP-IDF esp_lcd MIPI DSI / DPI 调用链（原 pal_mipi_dsi 封装已展开）：
 *            - esp_lcd_new_dsi_bus / esp_lcd_new_panel_dpi / esp_lcd_panel_init
 *            - esp_ldo_acquire_channel（DSI PHY LDO 通道 3，2500mV）
 *            - mipi_dsi_priv.h 私有结构访问 DSI HAL：LL 视频模式 / 时钟 lane / cmd ACK
 *            - mipi_dsi_hal_host_gen_write_long_packet 发送 Generic Long Write (DT=0x29)
 *              写 TC358762 寄存器（无公共 API）
 *
 *          TC358762 寄存器通过 DSI Generic Long Write 6 字节包配置，不支持 I2C。
 *          本驱动封装 DSI 主机初始化 + 桥寄存器初始化 + 帧缓冲绘图操作 + 直接 PWM
 *          背光（ATTINY88 板不启用，保留 direct-PWM 路径）。
 *
 *          初始化顺序（由 rpi7pin 聚合层编排）：
 *            1. bsp_tc358762_init（DSI 主机 + DPI 面板 + 帧缓冲 + LDO）
 *            2. attiny88 释放桥复位
 *            3. bsp_tc358762_config_bridge（写桥寄存器序列 + 启动 PPI/DSI）
 *
 *          仅 bsp_display_rpi7pin.c 聚合层可包含本文件。
 *
 *          参考：Linux drivers/gpu/drm/bridge/tc358762.c + 树莓派 7" DSI 屏文档
 *          参考：ESP32-P4 TRM MIPI DSI 章节
 *
 * @author  xLumina
 * @version 2.0
 */
#ifndef BSP_DISPLAY_TC358762_H
#define BSP_DISPLAY_TC358762_H

#include "dal_err.h"
#include "esp_lcd_mipi_dsi.h"      /* esp_lcd_dsi_bus_handle_t / esp_lcd_panel_handle_t */
#include "esp_ldo_regulator.h"     /* esp_ldo_channel_handle_t（DSI PHY LDO） */
#include "driver/ledc.h"           /* ledc_mode_t（直接 PWM 背光） */
#include <stdint.h>
#include <stdbool.h>

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

/**
 * @brief TC358762 桥接芯片驱动上下文（BSP 私有）
 *
 * @details 内联自原 pal_mipi_dsi_internal_t：DSI 总线 / DPI 面板 / 帧缓冲 /
 *          LDO 通道 / 直接 PWM 背光状态。BSP 业务字段（vc/width/height/bpp/inited）
 *          保留。
 */
typedef struct {
    /* ---- DSI 主机 + DPI 面板（内联自 pal_mipi_dsi_internal_t）---- */
    esp_lcd_dsi_bus_handle_t    dsi_bus;        /**< DSI 总线句柄 */
    esp_lcd_panel_handle_t      panel;          /**< DPI 面板句柄 */
    void                       *fb[2];          /**< 帧缓冲指针数组 */
    int                         fb_count;       /**< 实际帧缓冲数量 */
    esp_ldo_channel_handle_t    ldo;            /**< DSI PHY LDO 通道句柄（通道 3） */

    /* ---- 直接 PWM 背光（ATTINY88 板不启用，保留 direct-PWM 路径）---- */
    bool                        bl_configured;  /**< 背光 LEDC 是否已配置 */
    int                         bl_channel;     /**< 背光 LEDC 通道 */
    ledc_mode_t                 bl_speed_mode;  /**< 背光 LEDC 速度模式 */

    /* ---- BSP 业务字段 ---- */
    uint8_t                     vc;             /**< DSI 虚拟通道 */
    uint16_t                    width;          /**< 水平分辨率 */
    uint16_t                    height;         /**< 垂直分辨率 */
    int                         bpp;            /**< 每像素字节数（来自 in_color_format） */
    bool                        inited;         /**< 是否已初始化 */
} bsp_tc358762_ctx_t;

/**
 * @brief 初始化 DSI 主机 + DPI 面板（不含桥寄存器）
 *
 * @details 内联原 pal_mipi_dsi_init 调用链：LDO 供电 → 创建 DSI 总线 →
 *          临时 DBI IO 锁存 LP 命令通道 → 创建 DPI 面板 → DSI 主机 LL 预配置 →
 *          esp_lcd_panel_init → DSI 主机 LL 后配置 → 获取帧缓冲 → 直接 PWM 背光。
 *          引脚/时序/DSI 参数取自 board_v1_config.h 宏。
 *
 * @param[in,out] ctx 上下文（由调用方分配）
 * @return DAL_OK 成功，DAL_ERR_HW 底层错误，DAL_ERR_INVALID 参数非法
 *
 * @note 调用后需先由 attiny88 释放桥复位，再调 bsp_tc358762_config_bridge()
 *       配置桥寄存器。LDO 申请失败非致命（可能已被系统初始化引用计数），继续。
 */
dal_err_t bsp_tc358762_init(bsp_tc358762_ctx_t *ctx);

/**
 * @brief 写 TC358762 桥寄存器初始化序列并启动 PPI/DSI
 *
 * @details 必须在 DSI 主机初始化、ATTINY88 释放桥复位之后调用。
 *          经 DSI Generic Long Write 写入 DSI 通道使能、PPI、LCD 控制、
 *          系统控制、时序寄存器，最后启动 PPI/DSI。
 *
 * @param[in] ctx 上下文
 * @return DAL_OK 成功
 */
dal_err_t bsp_tc358762_config_bridge(bsp_tc358762_ctx_t *ctx);

/**
 * @brief 反初始化（停桥接 + 销毁面板/DSI 总线 + 释放 LDO）
 * @param[in] ctx 上下文
 * @return DAL_OK 成功
 */
dal_err_t bsp_tc358762_deinit(bsp_tc358762_ctx_t *ctx);

/**
 * @brief 用纯色填充矩形区域（直接写帧缓冲 + draw_bitmap 触发 cache write-back）
 * @param[in] ctx   上下文
 * @param[in] x,y,w,h 矩形区域
 * @param[in] color 填充色（格式与 in_color_format 匹配）
 * @return DAL_OK 成功
 */
dal_err_t bsp_tc358762_fill(bsp_tc358762_ctx_t *ctx,
                             uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief 在指定区域绘制位图（逐行 memcpy 到 fb[0] + draw_bitmap 触发 write-back）
 * @param[in] ctx  上下文
 * @param[in] x,y  区域左上角
 * @param[in] w,h  区域宽高
 * @param[in] data 像素数据（格式与 in_color_format 匹配）
 * @return DAL_OK 成功
 */
dal_err_t bsp_tc358762_draw_bitmap(bsp_tc358762_ctx_t *ctx,
                                   uint16_t x, uint16_t y,
                                   uint16_t w, uint16_t h, const void *data);

/**
 * @brief 按索引获取帧缓冲指针
 * @param[in]  ctx   上下文
 * @param[in]  index fb 索引（0 或 1，双缓冲）
 * @param[out] fb    返回该 fb 起始地址
 * @return DAL_OK 成功，DAL_ERR_INVALID 索引越界
 */
dal_err_t bsp_tc358762_get_fb(bsp_tc358762_ctx_t *ctx, uint8_t index, void **fb);

/**
 * @brief 设置背光亮度（直接 PWM 路径，需 BOARD_DISPLAY_BL_USE_DIRECT_PWM=true）
 *
 * @details ATTINY88 板不启用此路径（背光由 ATTINY88 控制），调用返回
 *          DAL_ERR_INVALID。direct-PWM 板按 10-bit 占空比映射百分比。
 *
 * @param[in] ctx     上下文
 * @param[in] percent 亮度百分比 0~100
 * @return DAL_OK 成功，DAL_ERR_INVALID 未配置直接 PWM 背光
 */
dal_err_t bsp_tc358762_set_backlight(bsp_tc358762_ctx_t *ctx, uint8_t percent);

#ifdef __cplusplus
}
#endif
#endif /* BSP_DISPLAY_TC358762_H */
