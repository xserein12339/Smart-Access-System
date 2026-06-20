/**
 * @file    pal_mipi_dsi.c
 * @brief   PAL MIPI DSI 模块 - 实现（ESP-IDF esp_lcd MIPI DSI 封装）
 *
 * 使用 ESP-IDF esp_lcd 组件（esp_lcd_mipi_dsi.h + esp_lcd_panel_ops.h）。
 * 内部管理 DSI 总线句柄 + DPI 面板句柄 + 帧缓冲 + 背光 LEDC 通道。
 */

#include "pal_mipi_dsi.h"

#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "soc/clk_tree_defs.h"
#include "hal/mipi_dsi_hal.h"           /* mipi_dsi_hal_host_gen_write_long_packet */
#include "mipi_dsi_priv.h"             /* struct esp_lcd_dsi_bus_t (已加入 INCLUDE_DIRS) */
#include "pal_ldo.h"                   /* DSI PHY LDO 供电（PAL 封装） */
#include "hal/mipi_dsi_host_ll.h"      /* DSI host LL: video mode, clock lane, frame ACK */

#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  内部结构体
 * ================================================================ */

/**
 * @brief DSI 显示器内部状态
 */
typedef struct {
    esp_lcd_dsi_bus_handle_t    dsi_bus;        /**< DSI 总线句柄 */
    esp_lcd_panel_io_handle_t   panel_io;       /**< Panel IO（DBI 命令接口） */
    esp_lcd_panel_handle_t      panel;          /**< DPI 面板句柄 */
    pal_mipi_dsi_config_t       cfg;            /**< 配置副本 */
    void                       *fb[3];          /**< 帧缓冲指针数组（最多 3 个） */
    int                         fb_count;       /**< 实际帧缓冲数量 */
    pal_ldo_handle_t           ldo;            /**< DSI PHY LDO 通道句柄 */

    /* 背光 PWM 状态 */
    bool                        bl_configured;   /**< 背光是否已配置 */
    int                         bl_channel;      /**< 背光 LEDC 通道 */
    ledc_mode_t                 bl_speed_mode;   /**< 背光 LEDC 速度模式 */
} pal_mipi_dsi_internal_t;

/* ================================================================
 *  内部枚举映射
 * ================================================================ */

/** @brief PAL 像素格式 → ESP-IDF lcd_color_rgb_pixel_format_t */
static lcd_color_rgb_pixel_format_t pal_to_esp_pixel_fmt(pal_mipi_dsi_color_fmt_t fmt)
{
    switch (fmt) {
    case PAL_DSI_COLOR_RGB565: return LCD_COLOR_PIXEL_FORMAT_RGB565;
    case PAL_DSI_COLOR_RGB666: return LCD_COLOR_PIXEL_FORMAT_RGB666;
    case PAL_DSI_COLOR_RGB888: return LCD_COLOR_PIXEL_FORMAT_RGB888;
    default:                   return LCD_COLOR_PIXEL_FORMAT_RGB565;
    }
}

/** @brief PAL 输入颜色格式 → ESP-IDF lcd_color_format_t */
static lcd_color_format_t pal_to_esp_color_fmt(pal_mipi_dsi_in_color_fmt_t fmt)
{
    switch (fmt) {
    case PAL_DSI_IN_COLOR_RGB565: return LCD_COLOR_FMT_RGB565;
    case PAL_DSI_IN_COLOR_RGB888: return LCD_COLOR_FMT_RGB888;
    default:                      return LCD_COLOR_FMT_RGB565;
    }
}

/** @brief 根据颜色格式返回每像素字节数 */
static int bpp_from_format(pal_mipi_dsi_in_color_fmt_t fmt)
{
    switch (fmt) {
    case PAL_DSI_IN_COLOR_RGB565: return 2;
    case PAL_DSI_IN_COLOR_RGB888: return 3;
    default:                      return 2;
    }
}

/* ================================================================
 *  生命周期
 * ================================================================ */

int pal_mipi_dsi_init(pal_mipi_dsi_handle_t *handle,
                      const pal_mipi_dsi_config_t *cfg)
{
    if (handle == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    pal_mipi_dsi_internal_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(&ctx->cfg, cfg, sizeof(*cfg));
    ctx->bl_configured = false;
    ctx->fb_count      = 0;

    /* ---- 0. DSI PHY LDO 供电（必须在创建总线前，通道3→2500mV） ---- */
    esp_err_t ret;
    pal_ldo_config_t ldo_cfg = {
        .chan_id    = 3,
        .voltage_mv = 2500,
    };
    ret = pal_ldo_acquire(&ctx->ldo, &ldo_cfg);
    /* LDO 通道可能已在系统初始化时被申请（底层引用计数），失败不致命，继续 */

    /* ---- 1. 创建 DSI 总线 ---- */
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id             = cfg->dsi_host,
        .num_data_lanes     = cfg->num_data_lanes,
        .phy_clk_src        = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT_LEGACY, /* PLL_F20M，兼容 rev < 3.0 */
        .lane_bit_rate_mbps = (float)cfg->lane_bit_rate_mbps,
    };

    ret = esp_lcd_new_dsi_bus(&bus_cfg, &ctx->dsi_bus);
    if (ret != ESP_OK) {
        if (ctx->ldo) {
            pal_ldo_release(ctx->ldo);
        }
        free(ctx);
        return ret;
    }

    /* ---- 2. 创建并删除临时 DBI IO，锁存 DSI LP 命令通道配置 ---- */
    esp_lcd_dbi_io_config_t io_cfg = {
        .virtual_channel = cfg->virtual_channel,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };

    ret = esp_lcd_new_panel_io_dbi(ctx->dsi_bus, &io_cfg, &ctx->panel_io);
    if (ret != ESP_OK) {
        esp_lcd_del_dsi_bus(ctx->dsi_bus);
        if (ctx->ldo) {
            pal_ldo_release(ctx->ldo);
        }
        free(ctx);
        return ret;
    }
    esp_lcd_panel_io_del(ctx->panel_io);
    ctx->panel_io = NULL;

    /* ---- 3. 创建 DPI 面板（视频模式） ---- */
    esp_lcd_dpi_panel_config_t panel_cfg = {
        .virtual_channel    = cfg->virtual_channel,
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = cfg->dpi_clock_freq_mhz, /* 树莓派 7" = 25.98 */
        .pixel_format       = pal_to_esp_pixel_fmt(cfg->pixel_format),
        .in_color_format    = pal_to_esp_color_fmt(cfg->in_color_format),
        .out_color_format   = pal_to_esp_color_fmt(cfg->in_color_format),
        .num_fbs            = (cfg->num_fbs > 0) ? cfg->num_fbs : 2,
        .video_timing = {
            .h_size            = cfg->h_res,
            .v_size            = cfg->v_res,
            .hsync_pulse_width = cfg->timing.hsync_pulse_width,
            .hsync_back_porch  = cfg->timing.hsync_back_porch,
            .hsync_front_porch = cfg->timing.hsync_front_porch,
            .vsync_pulse_width = cfg->timing.vsync_pulse_width,
            .vsync_back_porch  = cfg->timing.vsync_back_porch,
            .vsync_front_porch = cfg->timing.vsync_front_porch,
        },
        .flags = {
            .use_dma2d  = cfg->use_dma2d ? 1 : 0,
            .disable_lp  = 1, /* 禁用 Low-Power 模式，确保 DPI 输出稳定 */
        },
    };

    ret = esp_lcd_new_panel_dpi(ctx->dsi_bus, &panel_cfg, &ctx->panel);
    if (ret != ESP_OK) {
        esp_lcd_del_dsi_bus(ctx->dsi_bus);
        if (ctx->ldo) {
            pal_ldo_release(ctx->ldo);
        }
        free(ctx);
        return ret;
    }

    /* ---- 3a. DSI 主机预配置（面板 init 前必须设置） ---- */
    {
        struct esp_lcd_dsi_bus_t *bus = (struct esp_lcd_dsi_bus_t *)ctx->dsi_bus;
        /* Non-Burst + Sync Pulses 视频模式（TC358762 要求） */
        mipi_dsi_host_ll_dpi_set_video_burst_type(bus->hal.host,
            MIPI_DSI_LL_VIDEO_NON_BURST_WITH_SYNC_PULSES);
        /* 关闭 Frame ACK（TC358762 不响应） */
        mipi_dsi_host_ll_dpi_enable_frame_ack(bus->hal.host, false);
    }

    /* DPI 面板无独立 reset 回调，直接执行初始化，避免 ESP-IDF 打印 unsupported 错误。 */
    esp_lcd_panel_init(ctx->panel);

    /* ---- 3b. DSI 主机后配置（面板 init 后设置） ---- */
    {
        struct esp_lcd_dsi_bus_t *bus = (struct esp_lcd_dsi_bus_t *)ctx->dsi_bus;
        /* 强制 DSI 时钟 HS 连续模式（TC358762 FLL 需要稳定参考时钟） */
        mipi_dsi_host_ll_set_clock_lane_state(bus->hal.host,
            MIPI_DSI_LL_CLOCK_LANE_STATE_HS);
        /* 关闭 CMD ACK / BTA（避免 LP 通道竞争） */
        mipi_dsi_host_ll_enable_cmd_ack(bus->hal.host, false);
    }

    /* ---- 4. 获取帧缓冲指针 ---- */
    /* fb_num = 要获取的缓冲数量（不是索引！必须 ≥ 1） */
    ctx->fb_count = (cfg->num_fbs > 3) ? 3
                   : ((cfg->num_fbs > 0) ? cfg->num_fbs : 2);
    if (ctx->fb_count == 2) {
        esp_lcd_dpi_panel_get_frame_buffer(ctx->panel, 2,
                                            &ctx->fb[0], &ctx->fb[1]);
    } else if (ctx->fb_count == 1) {
        esp_lcd_dpi_panel_get_frame_buffer(ctx->panel, 1,
                                            &ctx->fb[0]);
    }

    /* ---- 5. 背光 PWM 初始化 ---- */
    if (cfg->bl_enabled && cfg->bl_gpio >= 0) {
        ledc_timer_config_t timer_cfg = {
            .speed_mode      = LEDC_LOW_SPEED_MODE,
            .timer_num       = (ledc_timer_t)cfg->bl_ledc_timer,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .freq_hz         = cfg->bl_freq_hz > 0 ? cfg->bl_freq_hz : 5000,
            .clk_cfg         = LEDC_AUTO_CLK,
        };
        ret = ledc_timer_config(&timer_cfg);
        if (ret == ESP_OK) {
            ledc_channel_config_t ch_cfg = {
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .channel    = (ledc_channel_t)cfg->bl_ledc_channel,
                .timer_sel  = (ledc_timer_t)cfg->bl_ledc_timer,
                .intr_type  = LEDC_INTR_DISABLE,
                .gpio_num   = cfg->bl_gpio,
                .duty       = 512, /* 50% 初始亮度 */
                .hpoint     = 0,
                .flags      = { .output_invert = 0 },
            };
            ret = ledc_channel_config(&ch_cfg);
            if (ret == ESP_OK) {
                ctx->bl_configured = true;
                ctx->bl_channel    = cfg->bl_ledc_channel;
                ctx->bl_speed_mode = LEDC_LOW_SPEED_MODE;
            }
        }
    }

    *handle = (pal_mipi_dsi_handle_t)ctx;
    return ESP_OK;
}

int pal_mipi_dsi_deinit(pal_mipi_dsi_handle_t handle)
{
    pal_mipi_dsi_internal_t *ctx = (pal_mipi_dsi_internal_t *)handle;
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 关闭背光 */
    if (ctx->bl_configured) {
        ledc_stop(ctx->bl_speed_mode, (ledc_channel_t)ctx->bl_channel, 0);
    }

    /* 销毁面板和 DSI 总线 */
    if (ctx->panel != NULL) {
        esp_lcd_panel_del(ctx->panel);
    }
    if (ctx->panel_io != NULL) {
        esp_lcd_panel_io_del(ctx->panel_io);
    }
    if (ctx->dsi_bus != NULL) {
        esp_lcd_del_dsi_bus(ctx->dsi_bus);
    }

    if (ctx->ldo != NULL) {
        pal_ldo_release(ctx->ldo);
    }

    free(ctx);
    return ESP_OK;
}

/* ================================================================
 *  显示控制
 * ================================================================ */

int pal_mipi_dsi_display_on(pal_mipi_dsi_handle_t handle)
{
    pal_mipi_dsi_internal_t *ctx = (pal_mipi_dsi_internal_t *)handle;
    if (ctx == NULL || ctx->panel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* DPI 面板视频流已在 init 阶段启动，disp_on_off 对该面板不支持。 */
    return ESP_OK;
}

int pal_mipi_dsi_display_off(pal_mipi_dsi_handle_t handle)
{
    pal_mipi_dsi_internal_t *ctx = (pal_mipi_dsi_internal_t *)handle;
    if (ctx == NULL || ctx->panel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* DPI 面板无独立 display off 回调，电源/背光由 DAL/ATTINY88 管理。 */
    return ESP_OK;
}

/* ================================================================
 *  绘制
 * ================================================================ */

int pal_mipi_dsi_draw_bitmap(pal_mipi_dsi_handle_t handle,
                             uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h,
                             const void *data)
{
    pal_mipi_dsi_internal_t *ctx = (pal_mipi_dsi_internal_t *)handle;
    if (ctx == NULL || ctx->panel == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (w == 0 || h == 0) {
        return ESP_OK;
    }

    return esp_lcd_panel_draw_bitmap(ctx->panel,
                                     (int)x, (int)y,
                                     (int)(x + w), (int)(y + h),
                                     data);
}

int pal_mipi_dsi_fill(pal_mipi_dsi_handle_t handle,
                      uint16_t x, uint16_t y,
                      uint16_t w, uint16_t h,
                      uint32_t color)
{
    pal_mipi_dsi_internal_t *ctx = (pal_mipi_dsi_internal_t *)handle;
    if (ctx == NULL || ctx->panel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (w == 0 || h == 0) {
        return ESP_OK;
    }

    int bpp = bpp_from_format(ctx->cfg.in_color_format);
    uint8_t *fb = (uint8_t *)ctx->fb[0];
    if (fb == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 直接写帧缓冲，再走 draw_bitmap 的零拷贝路径触发 cache write-back。 */
    if (bpp >= 3) {
        uint8_t r = (uint8_t)((color >> 16) & 0xFF);
        uint8_t g = (uint8_t)((color >> 8) & 0xFF);
        uint8_t b_val = (uint8_t)(color & 0xFF);
        for (uint16_t row_y = 0; row_y < h; row_y++) {
            uint8_t *dst = fb + (((size_t)y + row_y) * ctx->cfg.h_res + x) * bpp;
            for (uint16_t col = 0; col < w; col++) {
                dst[col * 3 + 0] = b_val;
                dst[col * 3 + 1] = g;
                dst[col * 3 + 2] = r;
            }
        }
    } else {
        uint8_t hi = (uint8_t)((color >> 8) & 0xFF);
        uint8_t lo = (uint8_t)(color & 0xFF);
        for (uint16_t row_y = 0; row_y < h; row_y++) {
            uint8_t *dst = fb + (((size_t)y + row_y) * ctx->cfg.h_res + x) * bpp;
            for (uint16_t col = 0; col < w; col++) {
                dst[col * 2 + 0] = hi;
                dst[col * 2 + 1] = lo;
            }
        }
    }

    return esp_lcd_panel_draw_bitmap(ctx->panel,
                                     (int)x, (int)y,
                                     (int)(x + w), (int)(y + h),
                                     fb + ((size_t)y * ctx->cfg.h_res + x) * bpp);
}

int pal_mipi_dsi_get_fb(pal_mipi_dsi_handle_t handle,
                        void **fb0, void **fb1)
{
    pal_mipi_dsi_internal_t *ctx = (pal_mipi_dsi_internal_t *)handle;
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (fb0 != NULL) {
        *fb0 = ctx->fb[0];
    }
    if (fb1 != NULL) {
        *fb1 = (ctx->fb_count > 1) ? ctx->fb[1] : NULL;
    }
    return ESP_OK;
}

/* ================================================================
 *  背光
 * ================================================================ */

int pal_mipi_dsi_set_backlight(pal_mipi_dsi_handle_t handle,
                               uint8_t percent)
{
    pal_mipi_dsi_internal_t *ctx = (pal_mipi_dsi_internal_t *)handle;
    if (ctx == NULL || !ctx->bl_configured) {
        return ESP_ERR_INVALID_ARG;
    }
    if (percent > 100) {
        percent = 100;
    }

    /* 10-bit 占空比分辨率：0 ~ 1023 */
    uint32_t max_duty = 1023;
    uint32_t duty = (uint32_t)((uint64_t)max_duty * percent / 100);

    esp_err_t ret = ledc_set_duty(ctx->bl_speed_mode,
                                   (ledc_channel_t)ctx->bl_channel,
                                   duty);
    if (ret != ESP_OK) {
        return ret;
    }
    return ledc_update_duty(ctx->bl_speed_mode,
                            (ledc_channel_t)ctx->bl_channel);
}

/* ================================================================
 *  DSI 原始命令
 * ================================================================ */

int pal_mipi_dsi_send_generic_write(pal_mipi_dsi_handle_t handle,
                                    uint8_t vc,
                                    const uint8_t *payload,
                                    size_t len)
{
    pal_mipi_dsi_internal_t *ctx = (pal_mipi_dsi_internal_t *)handle;
    if (ctx == NULL || ctx->dsi_bus == NULL || payload == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 通过私有结构体访问 DSI HAL，发送 Generic Long Write (DT=0x29) */
    struct esp_lcd_dsi_bus_t *bus = (struct esp_lcd_dsi_bus_t *)ctx->dsi_bus;
    mipi_dsi_hal_host_gen_write_long_packet(&bus->hal, vc,
                                            MIPI_DSI_DT_GENERIC_LONG_WRITE,
                                            payload, len);
    return ESP_OK;
}
