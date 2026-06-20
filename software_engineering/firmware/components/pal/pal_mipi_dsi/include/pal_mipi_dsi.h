/**
 * @file    pal_mipi_dsi.h
 * @brief   PAL MIPI DSI 模块 — 液晶显示屏驱动
 *
 * 封装 ESP-IDF esp_lcd 组件（MIPI DSI + DPI 面板），提供：
 *   - DSI 总线初始化与配置
 *   - DPI 面板创建（分辨率、像素格式、帧缓冲）
 *   - 帧缓冲绘点 / 位图刷新
 *   - 背光 PWM 控制
 *
 * 面板初始化（如 TC358762 寄存器序列）由 DAL 层通过 pal_i2c 完成。
 * PAL 仅负责 DSI 主机传输、帧缓冲管理和背光控制。
 *
 * 参考文档：ESP32-P4 TRM MIPI DSI 章节
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef PAL_MIPI_DSI_H
#define PAL_MIPI_DSI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief DSI 显示器不透明句柄 */
typedef void *pal_mipi_dsi_handle_t;

/* ================================================================
 *  枚举类型
 * ================================================================ */

/** @brief DSI DPI 像素输出格式 */
typedef enum {
    PAL_DSI_COLOR_RGB565 = 0,   /**< RGB565（16 bpp） */
    PAL_DSI_COLOR_RGB666 = 1,   /**< RGB666（18 bpp） */
    PAL_DSI_COLOR_RGB888 = 2,   /**< RGB888（24 bpp） */
} pal_mipi_dsi_color_fmt_t;

/** @brief DSI DPI 输入颜色格式（帧缓冲格式） */
typedef enum {
    PAL_DSI_IN_COLOR_RGB565 = 0,   /**< 输入帧缓冲 RGB565 */
    PAL_DSI_IN_COLOR_RGB888 = 1,   /**< 输入帧缓冲 RGB888 */
} pal_mipi_dsi_in_color_fmt_t;

/* ================================================================
 *  显示时序结构体
 * ================================================================ */

/**
 * @brief 显示视频时序参数
 *
 * 参考 LCD 面板数据手册中的时序图填写，单位：像素时钟周期（水平）/ 行周期（垂直）。
 */
typedef struct {
    uint16_t h_res;             /**< 水平有效像素 */
    uint16_t v_res;             /**< 垂直有效像素 */
    uint16_t hsync_pulse_width; /**< 水平同步脉宽（像素时钟周期数） */
    uint16_t hsync_back_porch;  /**< 水平后肩 */
    uint16_t hsync_front_porch; /**< 水平前肩 */
    uint16_t vsync_pulse_width; /**< 垂直同步脉宽（行周期数） */
    uint16_t vsync_back_porch;  /**< 垂直后肩 */
    uint16_t vsync_front_porch; /**< 垂直前肩 */
    uint16_t hsync_polarity;    /**< 水平同步极性（0 = 低有效，1 = 高有效） */
    uint16_t vsync_polarity;    /**< 垂直同步极性（0 = 低有效，1 = 高有效） */
} pal_mipi_dsi_timing_t;

/* ================================================================
 *  配置结构体
 * ================================================================ */

/**
 * @brief MIPI DSI 显示初始化配置
 */
typedef struct {
    /* ---- DSI 总线参数 ---- */
    int         dsi_host;           /**< DSI 主机编号（0） */
    uint8_t     num_data_lanes;     /**< 数据通道数（1 或 2） */
    int         lane_bit_rate_mbps; /**< DSI 通道比特率（Mbps） */
    int         virtual_channel;    /**< 虚拟通道 ID（0 ~ 3） */

    /* ---- DPI 面板参数 ---- */
    uint16_t                    h_res;           /**< 水平有效像素 */
    uint16_t                    v_res;           /**< 垂直有效像素 */
    pal_mipi_dsi_color_fmt_t    pixel_format;    /**< 像素格式（DSI 输出） */
    pal_mipi_dsi_in_color_fmt_t in_color_format; /**< 输入帧缓冲颜色格式 */
    pal_mipi_dsi_timing_t       timing;          /**< 视频时序参数 */
    float                       dpi_clock_freq_mhz; /**< DPI 像素时钟 (MHz)，0=自动计算 */

    /* ---- 帧缓冲 ---- */
    int         num_fbs;            /**< 帧缓冲数量（1 = 单缓冲，2 = 双缓冲） */
    bool        use_dma2d;          /**< 是否使用 DMA2D 加速内存拷贝 */

    /* ---- 背光（可选，由 PAL 内部管理 LEDC） ---- */
    bool        bl_enabled;         /**< 是否启用背光 PWM */
    int         bl_gpio;            /**< 背光 GPIO 引脚 */
    int         bl_ledc_channel;    /**< 背光 LEDC 通道编号 */
    int         bl_ledc_timer;      /**< 背光 LEDC 定时器编号 */
    uint32_t    bl_freq_hz;         /**< 背光 PWM 频率（Hz） */
} pal_mipi_dsi_config_t;

/* ================================================================
 *  生命周期 API
 * ================================================================ */

/**
 * @brief 初始化 MIPI DSI 显示器
 *
 * 创建 DSI 总线 + DPI 面板，分配帧缓冲，配置背光 PWM。
 *
 * @param[out] handle 返回的显示器句柄
 * @param[in]  cfg    显示器配置
 * @return 0 成功，负数失败
 */
int pal_mipi_dsi_init(pal_mipi_dsi_handle_t *handle,
                      const pal_mipi_dsi_config_t *cfg);

/**
 * @brief 反初始化 DSI 显示器，释放帧缓冲及所有资源
 *
 * @param handle 显示器句柄
 * @return 0 成功
 */
int pal_mipi_dsi_deinit(pal_mipi_dsi_handle_t handle);

/* ================================================================
 *  显示控制 API
 * ================================================================ */

/**
 * @brief 打开显示（开始扫描输出）
 *
 * @param handle 显示器句柄
 * @return 0 成功
 */
int pal_mipi_dsi_display_on(pal_mipi_dsi_handle_t handle);

/**
 * @brief 关闭显示（停止扫描输出，保持背光可独立控制）
 *
 * @param handle 显示器句柄
 * @return 0 成功
 */
int pal_mipi_dsi_display_off(pal_mipi_dsi_handle_t handle);

/* ================================================================
 *  绘制 API
 * ================================================================ */

/**
 * @brief 在指定区域绘制位图
 *
 * @param handle 显示器句柄
 * @param x      起始 X 坐标（像素）
 * @param y      起始 Y 坐标（像素）
 * @param w      宽度（像素）
 * @param h      高度（像素）
 * @param data   像素数据（格式与 in_color_format 匹配）
 * @return 0 成功，负数失败
 */
int pal_mipi_dsi_draw_bitmap(pal_mipi_dsi_handle_t handle,
                             uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h,
                             const void *data);

/**
 * @brief 用纯色填充指定矩形区域
 *
 * @param handle 显示器句柄
 * @param x      起始 X 坐标
 * @param y      起始 Y 坐标
 * @param w      宽度
 * @param h      高度
 * @param color  填充颜色（格式与 in_color_format 匹配）
 * @return 0 成功
 */
int pal_mipi_dsi_fill(pal_mipi_dsi_handle_t handle,
                      uint16_t x, uint16_t y,
                      uint16_t w, uint16_t h,
                      uint32_t color);

/**
 * @brief 获取帧缓冲指针（供上层直接操作）
 *
 * @param handle 显示器句柄
 * @param[out] fb0 帧缓冲 0 的指针
 * @param[out] fb1 帧缓冲 1 的指针（双缓冲时有效，可为 NULL）
 * @return 0 成功
 */
int pal_mipi_dsi_get_fb(pal_mipi_dsi_handle_t handle,
                        void **fb0, void **fb1);

/* ================================================================
 *  背光 API
 * ================================================================ */

/**
 * @brief 设置背光亮度（百分比）
 *
 * @param handle  显示器句柄
 * @param percent 亮度百分比（0 = 关，100 = 最亮）
 * @return 0 成功
 */
int pal_mipi_dsi_set_backlight(pal_mipi_dsi_handle_t handle,
                               uint8_t percent);

/* ================================================================
 *  DSI 原始命令 API（供 DAL 层配置桥接器）
 * ================================================================ */

/**
 * @brief 发送 DSI Generic Long Write 包（DT=0x29）
 *
 * 用于向 MIPI DSI 外设（如 TC358762 桥接器）写入寄存器。
 * TC358762 仅响应 Generic Write，不响应 DCS Write。
 *
 * @param handle    DSI 显示器句柄
 * @param vc        虚拟通道（0 ~ 3）
 * @param payload   负载数据指针
 * @param len       负载长度（字节）
 * @return 0 成功，负数失败
 */
int pal_mipi_dsi_send_generic_write(pal_mipi_dsi_handle_t handle,
                                    uint8_t vc,
                                    const uint8_t *payload,
                                    size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PAL_MIPI_DSI_H */
