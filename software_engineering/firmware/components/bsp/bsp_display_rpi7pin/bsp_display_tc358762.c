/**
 * @file    bsp_display_tc358762.c
 * @brief   TC358762 DSI 桥接芯片子驱动实现
 *
 * @details 原 pal_mipi_dsi 封装已彻底内联：直接调用 ESP-IDF esp_lcd 接口
 *          （DSI bus / DPI panel / frame buffer / LDO / LEDC 背光），通过
 *          mipi_dsi_priv.h 私有结构访问 DSI HAL 完成主机 LL 配置与
 *          Generic Long Write 寄存器写入。所有 esp_err_t 在本边界翻译为
 *          dal_err_t，原始码不透传到聚合层。
 *
 *          参数取自 board_v1_config.h 的 BOARD_DISPLAY_* 宏。
 *
 * @author  xLumina
 * @version 2.0
 */
#include "bsp_display_tc358762.h"
#include "board_v1_config.h"
#include "dal_esp_err.h"
#include "esp_log.h"

#include "esp_lcd_panel_io.h"        /* esp_lcd_panel_io_del */
#include "esp_lcd_panel_ops.h"       /* esp_lcd_panel_init/del/draw_bitmap */
#include "esp_lcd_types.h"
#include "soc/clk_tree_defs.h"       /* MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT_LEGACY */
#include "hal/mipi_dsi_hal.h"        /* mipi_dsi_hal_host_gen_write_long_packet */
#include "hal/mipi_dsi_host_ll.h"    /* DSI host LL: video mode, clock lane, cmd ACK */
#include "mipi_dsi_priv.h"           /* struct esp_lcd_dsi_bus_t（私有，已加 INCLUDE_DIRS） */

#include <stdlib.h>
#include <string.h>

/* 注意：mipi_dsi_priv.h 内部 #define TAG "lcd.dsi"，故此处日志 tag 变量
 * 不能命名为 TAG（否则被宏展开为字符串字面量导致语法错误），改用 s_tag。 */
static const char *const s_tag = "TC358762";

/* ================================================================
 *  颜色格式映射（BOARD_DISPLAY_* 宏值 → ESP-IDF 枚举）
 *  宏值约定与原 pal_mipi_dsi 枚举一致：0=RGB565, 1=RGB666/RGB888-in, 2=RGB888
 * ================================================================ */

/** @brief 像素输出格式宏值 → lcd_color_rgb_pixel_format_t */
static lcd_color_rgb_pixel_format_t tc358762_pixel_fmt(int fmt)
{
    switch (fmt) {
    case 0: return LCD_COLOR_PIXEL_FORMAT_RGB565;
    case 1: return LCD_COLOR_PIXEL_FORMAT_RGB666;
    case 2: return LCD_COLOR_PIXEL_FORMAT_RGB888;
    default: return LCD_COLOR_PIXEL_FORMAT_RGB565;
    }
}

/** @brief 输入帧缓冲颜色格式宏值 → lcd_color_format_t */
static lcd_color_format_t tc358762_color_fmt(int fmt)
{
    switch (fmt) {
    case 0: return LCD_COLOR_FMT_RGB565;
    case 1: return LCD_COLOR_FMT_RGB888;
    default: return LCD_COLOR_FMT_RGB565;
    }
}

/** @brief 根据输入颜色格式宏值返回每像素字节数 */
static int tc358762_bpp(int fmt)
{
    switch (fmt) {
    case 0: return 2;   /* RGB565 */
    case 1: return 3;   /* RGB888 */
    default: return 2;
    }
}

/* ================================================================
 *  DSI Generic Long Write 寄存器写（TC358762 寄存器配置必需）
 * ================================================================ */

/**
 * @brief 经 DSI Generic Long Write (DT=0x29) 写 32-bit 值到 16-bit 寄存器
 *
 * @details payload[6] = { reg_lo, reg_hi, val_b0, val_b1, val_b2, val_b3 }
 *          通过 mipi_dsi_priv.h 私有结构访问 DSI HAL，无公共 API。
 *          mipi_dsi_hal_host_gen_write_long_packet 返回 void（硬件级发送）。
 */
static dal_err_t tc358762_write_reg(bsp_tc358762_ctx_t *ctx,
                                    uint16_t addr, uint32_t val)
{
    if (ctx == NULL || ctx->dsi_bus == NULL) {
        return DAL_ERR_INVALID;
    }
    uint8_t payload[6] = {
        (uint8_t)((addr >> 0) & 0xFF),
        (uint8_t)((addr >> 8) & 0xFF),
        (uint8_t)((val  >> 0)  & 0xFF),
        (uint8_t)((val  >> 8)  & 0xFF),
        (uint8_t)((val  >> 16) & 0xFF),
        (uint8_t)((val  >> 24) & 0xFF),
    };
    struct esp_lcd_dsi_bus_t *bus = (struct esp_lcd_dsi_bus_t *)ctx->dsi_bus;
    mipi_dsi_hal_host_gen_write_long_packet(&bus->hal, ctx->vc,
                                            MIPI_DSI_DT_GENERIC_LONG_WRITE,
                                            payload, sizeof(payload));
    return DAL_OK;
}

/* ================================================================
 *  生命周期：DSI 主机 + DPI 面板初始化（内联原 pal_mipi_dsi_init）
 * ================================================================ */

dal_err_t bsp_tc358762_init(bsp_tc358762_ctx_t *ctx)
{
    if (ctx == NULL) {
        return DAL_ERR_INVALID;
    }
    esp_err_t ret;

    /* ---- 0. DSI PHY LDO 供电（必须在创建总线前，通道 3 → 2500mV）----
     * 底层引用计数，可能已被系统初始化申请，失败非致命，继续。 */
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = 3,
        .voltage_mv = 2500,
    };
    ret = esp_ldo_acquire_channel(&ldo_cfg, &ctx->ldo);
    if (ret != ESP_OK) {
        ESP_LOGW(s_tag, "LDO ch3 acquire failed (non-fatal): %s", esp_err_to_name(ret));
        ctx->ldo = NULL;
    }

    /* ---- 1. 创建 DSI 总线 ---- */
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id             = BOARD_DISPLAY_DSI_HOST,
        .num_data_lanes     = BOARD_DISPLAY_DSI_NUM_DATA_LANES,
        .phy_clk_src        = 0, /* 0=由 esp_lcd_new_dsi_bus 按芯片版本自适应：
                                       rev<3.0 → PLL_F20M, rev≥3.0 → XTAL */
        .lane_bit_rate_mbps = (float)BOARD_DISPLAY_DSI_LANE_BIT_RATE_MBPS,
    };
    ret = esp_lcd_new_dsi_bus(&bus_cfg, &ctx->dsi_bus);
    if (ret != ESP_OK) {
        if (ctx->ldo) {
            esp_ldo_release_channel(ctx->ldo);
            ctx->ldo = NULL;
        }
        return dal_err_from_esp(ret);
    }

    /* ---- 2. 创建并删除临时 DBI IO，锁存 DSI LP 命令通道配置 ---- */
    {
        esp_lcd_dbi_io_config_t io_cfg = {
            .virtual_channel = BOARD_DISPLAY_DSI_VIRTUAL_CHANNEL,
            .lcd_cmd_bits    = 8,
            .lcd_param_bits  = 8,
        };
        esp_lcd_panel_io_handle_t io = NULL;
        ret = esp_lcd_new_panel_io_dbi(ctx->dsi_bus, &io_cfg, &io);
        if (ret != ESP_OK) {
            esp_lcd_del_dsi_bus(ctx->dsi_bus);
            ctx->dsi_bus = NULL;
            if (ctx->ldo) {
                esp_ldo_release_channel(ctx->ldo);
                ctx->ldo = NULL;
            }
            return dal_err_from_esp(ret);
        }
        esp_lcd_panel_io_del(io);
    }

    /* ---- 3. 创建 DPI 面板（视频模式）---- */
    esp_lcd_dpi_panel_config_t panel_cfg = {
        .virtual_channel    = BOARD_DISPLAY_DSI_VIRTUAL_CHANNEL,
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = BOARD_DISPLAY_DPI_CLOCK_FREQ_MHZ, /* 树莓派 7" = 25.98 */
        .pixel_format       = tc358762_pixel_fmt(BOARD_DISPLAY_PIXEL_FORMAT),
        .in_color_format    = tc358762_color_fmt(BOARD_DISPLAY_IN_COLOR_FORMAT),
        .out_color_format   = tc358762_color_fmt(BOARD_DISPLAY_IN_COLOR_FORMAT),
        .num_fbs            = (BOARD_DISPLAY_NUM_FBS > 0) ? BOARD_DISPLAY_NUM_FBS : 2,
        .video_timing = {
            .h_size            = BOARD_DISPLAY_H_RES,
            .v_size            = BOARD_DISPLAY_V_RES,
            .hsync_pulse_width = BOARD_DISPLAY_HSYNC_PULSE_WIDTH,
            .hsync_back_porch  = BOARD_DISPLAY_HSYNC_BACK_PORCH,
            .hsync_front_porch = BOARD_DISPLAY_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = BOARD_DISPLAY_VSYNC_PULSE_WIDTH,
            .vsync_back_porch  = BOARD_DISPLAY_VSYNC_BACK_PORCH,
            .vsync_front_porch = BOARD_DISPLAY_VSYNC_FRONT_PORCH,
        },
        .flags = {
            .use_dma2d  = 0,     /* BSP 不使用 DMA2D 拷贝 */
            .disable_lp = 1,     /* 禁用 Low-Power，确保 DPI 输出稳定 */
        },
    };
    ret = esp_lcd_new_panel_dpi(ctx->dsi_bus, &panel_cfg, &ctx->panel);
    if (ret != ESP_OK) {
        esp_lcd_del_dsi_bus(ctx->dsi_bus);
        ctx->dsi_bus = NULL;
        if (ctx->ldo) {
            esp_ldo_release_channel(ctx->ldo);
            ctx->ldo = NULL;
        }
        return dal_err_from_esp(ret);
    }

    /* ---- 3a. DSI 主机预配置（面板 init 前必须设置）---- */
    {
        struct esp_lcd_dsi_bus_t *bus = (struct esp_lcd_dsi_bus_t *)ctx->dsi_bus;
        /* Non-Burst + Sync Pulses 视频模式（TC358762 要求） */
        mipi_dsi_host_ll_dpi_set_video_burst_type(bus->hal.host,
            MIPI_DSI_LL_VIDEO_NON_BURST_WITH_SYNC_PULSES);
        /* 关闭 Frame ACK（TC358762 不响应） */
        mipi_dsi_host_ll_dpi_enable_frame_ack(bus->hal.host, false);
    }

    /* DPI 面板无独立 reset 回调，直接执行初始化，避免 ESP-IDF 打印 unsupported 错误。 */
    ret = esp_lcd_panel_init(ctx->panel);
    if (ret != ESP_OK) {
        esp_lcd_panel_del(ctx->panel);
        ctx->panel = NULL;
        esp_lcd_del_dsi_bus(ctx->dsi_bus);
        ctx->dsi_bus = NULL;
        if (ctx->ldo) {
            esp_ldo_release_channel(ctx->ldo);
            ctx->ldo = NULL;
        }
        return dal_err_from_esp(ret);
    }

    /* ---- 3b. DSI 主机后配置（面板 init 后设置）---- */
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
    ctx->fb_count = (BOARD_DISPLAY_NUM_FBS > 2) ? 2
                   : ((BOARD_DISPLAY_NUM_FBS > 0) ? BOARD_DISPLAY_NUM_FBS : 2);
    if (ctx->fb_count >= 2) {
        esp_lcd_dpi_panel_get_frame_buffer(ctx->panel, 2,
                                            &ctx->fb[0], &ctx->fb[1]);
    } else {
        esp_lcd_dpi_panel_get_frame_buffer(ctx->panel, 1, &ctx->fb[0]);
    }

    /* ---- 5. 直接 PWM 背光初始化（若启用 direct-PWM；ATTINY88 板不启用）---- */
    ctx->bl_configured = false;
    if (BOARD_DISPLAY_BL_USE_DIRECT_PWM && BOARD_DISPLAY_BL_GPIO >= 0) {
        ledc_timer_config_t timer_cfg = {
            .speed_mode      = LEDC_LOW_SPEED_MODE,
            .timer_num       = (ledc_timer_t)BOARD_DISPLAY_BL_LEDC_TIMER,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .freq_hz         = BOARD_DISPLAY_BL_FREQ_HZ > 0 ? BOARD_DISPLAY_BL_FREQ_HZ : 5000,
            .clk_cfg         = LEDC_AUTO_CLK,
        };
        ret = ledc_timer_config(&timer_cfg);
        if (ret == ESP_OK) {
            ledc_channel_config_t ch_cfg = {
                .speed_mode = LEDC_LOW_SPEED_MODE,
                .channel    = (ledc_channel_t)BOARD_DISPLAY_BL_LEDC_CHANNEL,
                .timer_sel  = (ledc_timer_t)BOARD_DISPLAY_BL_LEDC_TIMER,
                .intr_type  = LEDC_INTR_DISABLE,
                .gpio_num   = BOARD_DISPLAY_BL_GPIO,
                .duty       = 512, /* 50% 初始亮度 */
                .hpoint     = 0,
                .flags      = { .output_invert = 0 },
            };
            ret = ledc_channel_config(&ch_cfg);
            if (ret == ESP_OK) {
                ctx->bl_configured = true;
                ctx->bl_channel    = BOARD_DISPLAY_BL_LEDC_CHANNEL;
                ctx->bl_speed_mode = LEDC_LOW_SPEED_MODE;
            }
        }
    }

    /* ---- BSP 业务字段 ---- */
    ctx->vc     = (uint8_t)BOARD_DISPLAY_DSI_VIRTUAL_CHANNEL;
    ctx->width  = BOARD_DISPLAY_H_RES;
    ctx->height = BOARD_DISPLAY_V_RES;
    ctx->bpp    = tc358762_bpp(BOARD_DISPLAY_IN_COLOR_FORMAT);
    ctx->inited = true;

    ESP_LOGI(s_tag, "DSI host initialized (%ux%u)", ctx->width, ctx->height);
    return DAL_OK;
}

dal_err_t bsp_tc358762_config_bridge(bsp_tc358762_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }

    /* 时序参数（取自板级配置） */
    const uint16_t hpw  = BOARD_DISPLAY_HSYNC_PULSE_WIDTH;
    const uint16_t hbp  = BOARD_DISPLAY_HSYNC_BACK_PORCH;
    const uint16_t hfp  = BOARD_DISPLAY_HSYNC_FRONT_PORCH;
    const uint16_t vpw  = BOARD_DISPLAY_VSYNC_PULSE_WIDTH;
    const uint16_t vbp  = BOARD_DISPLAY_VSYNC_BACK_PORCH;
    const uint16_t vfp  = BOARD_DISPLAY_VSYNC_FRONT_PORCH;
    const uint16_t hres = ctx->width;
    const uint16_t vres = ctx->height;

    dal_err_t ret;
    /* DSI 通道使能：Clock + D0 */
    ret = tc358762_write_reg(ctx, TC358762_REG_DSI_LANEENABLE,
                             DSI_LANEENABLE_CLOCK | DSI_LANEENABLE_D0);
    if (ret != DAL_OK) return ret;

    /* PPI 配置 */
    ret  = tc358762_write_reg(ctx, TC358762_REG_CLRSIPOCOUNT, 0x05);
    ret |= tc358762_write_reg(ctx, TC358762_REG_ATMR,        0x00);
    ret |= tc358762_write_reg(ctx, TC358762_REG_LPTXTIMECNT, 0x04);
    if (ret != DAL_OK) return ret;

    /* LCD 控制 + 系统控制 */
    ret  = tc358762_write_reg(ctx, TC358762_REG_LCDCTRL, 0x00100150);
    ret |= tc358762_write_reg(ctx, TC358762_REG_SYSCTRL, 0x040F);
    if (ret != DAL_OK) return ret;

    /* 时序寄存器 */
    ret  = tc358762_write_reg(ctx, TC358762_REG_HS_HBP,
                              ((uint32_t)hpw << 16) | (hpw + hbp));
    ret |= tc358762_write_reg(ctx, TC358762_REG_HDISP_HFP,
                              ((uint32_t)(hpw + hbp + hres) << 16)
                              | (hpw + hbp + hres + hfp));
    ret |= tc358762_write_reg(ctx, TC358762_REG_VS_VBP,
                              ((uint32_t)vpw << 16) | (vpw + vbp));
    ret |= tc358762_write_reg(ctx, TC358762_REG_VDISP_VFP,
                              ((uint32_t)(vpw + vbp + vres) << 16)
                              | (vpw + vbp + vres + vfp));
    if (ret != DAL_OK) return ret;

    /* 启动 PPI / DSI */
    ret  = tc358762_write_reg(ctx, TC358762_REG_PPI_STARTPPI, 1);
    ret |= tc358762_write_reg(ctx, TC358762_REG_DSI_STARTDSI, 1);
    if (ret != DAL_OK) return ret;

    ESP_LOGI(s_tag, "bridge configured & started");
    return DAL_OK;
}

dal_err_t bsp_tc358762_deinit(bsp_tc358762_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }
    /* 停桥接 */
    tc358762_write_reg(ctx, TC358762_REG_SYSCTRL, 0x0400);

    /* 关背光 */
    if (ctx->bl_configured) {
        ledc_stop(ctx->bl_speed_mode, (ledc_channel_t)ctx->bl_channel, 0);
    }

    /* 销毁面板和 DSI 总线 */
    if (ctx->panel != NULL) {
        esp_lcd_panel_del(ctx->panel);
        ctx->panel = NULL;
    }
    if (ctx->dsi_bus != NULL) {
        esp_lcd_del_dsi_bus(ctx->dsi_bus);
        ctx->dsi_bus = NULL;
    }
    if (ctx->ldo != NULL) {
        esp_ldo_release_channel(ctx->ldo);
        ctx->ldo = NULL;
    }

    ctx->inited = false;
    return DAL_OK;
}

/* ================================================================
 *  绘制（内联原 pal_mipi_dsi_fill / draw_bitmap / get_fb）
 * ================================================================ */

dal_err_t bsp_tc358762_fill(bsp_tc358762_ctx_t *ctx,
                             uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h, uint16_t color)
{
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }
    if (w == 0 || h == 0) {
        return DAL_OK;
    }

    int bpp = ctx->bpp;
    uint8_t *fb = (uint8_t *)ctx->fb[0];
    if (fb == NULL) {
        return DAL_ERR_HW;
    }

    /* color 为 RGB565（R5G6B5）。fb 字节序为 RGB888 R,G,B（已验证）。
     * ⚠️ DPI panel（视频模式）持续从内部 fb DMA 刷新到屏，
     * esp_lcd_panel_draw_bitmap 对 DPI panel 仅对 fb 做 cache write-back。
     * 故先直接写 fb[0] 对应矩形区，再调 draw_bitmap(fb+offset) 触发 write-back。 */
    if (bpp >= 3) {
        /* RGB565 → RGB888：R5→8bit(×8+7)、G6→8bit(×4+3)、B5→8bit(×8+7) */
        uint8_t r = (uint8_t)(((color >> 11) & 0x1F) << 3 | 0x07);
        uint8_t g = (uint8_t)(((color >> 5)  & 0x3F) << 2 | 0x03);
        uint8_t b_val = (uint8_t)((color & 0x1F) << 3 | 0x07);
        for (uint16_t row_y = 0; row_y < h; row_y++) {
            uint8_t *dst = fb + (((size_t)y + row_y) * ctx->width + x) * bpp;
            for (uint16_t col = 0; col < w; col++) {
                dst[col * 3 + 0] = r;       /* R */
                dst[col * 3 + 1] = g;       /* G */
                dst[col * 3 + 2] = b_val;   /* B */
            }
        }
    } else {
        /* RGB565 LE：高字节在前 */
        uint8_t hi = (uint8_t)((color >> 8) & 0xFF);
        uint8_t lo = (uint8_t)(color & 0xFF);
        for (uint16_t row_y = 0; row_y < h; row_y++) {
            uint8_t *dst = fb + (((size_t)y + row_y) * ctx->width + x) * bpp;
            for (uint16_t col = 0; col < w; col++) {
                dst[col * 2 + 0] = hi;
                dst[col * 2 + 1] = lo;
            }
        }
    }

    return dal_err_from_esp(esp_lcd_panel_draw_bitmap(ctx->panel,
                             (int)x, (int)y,
                             (int)(x + w), (int)(y + h),
                             fb + ((size_t)y * ctx->width + x) * bpp));
}

dal_err_t bsp_tc358762_draw_bitmap(bsp_tc358762_ctx_t *ctx,
                                   uint16_t x, uint16_t y,
                                   uint16_t w, uint16_t h, const void *data)
{
    if (ctx == NULL || !ctx->inited || data == NULL) {
        return DAL_ERR_INVALID;
    }
    if (w == 0 || h == 0) {
        return DAL_OK;
    }

    /* data 须为 framebuffer 指针（LVGL DIRECT 模式下 draw buffer 即 DPI fb）。
     * DPI panel 检测到 color_data 在 fb 范围内 → 仅 cache writeback + swap
     * （cur_fb_index 切换），vsync 交换免撕裂。不再手动 memcpy 到 fb[0]
     * （旧法写显示中的 fb 会撕裂）。 */
    return dal_err_from_esp(esp_lcd_panel_draw_bitmap(ctx->panel,
                             (int)x, (int)y,
                             (int)(x + w), (int)(y + h),
                             data));
}

dal_err_t bsp_tc358762_get_fb(bsp_tc358762_ctx_t *ctx, uint8_t index, void **fb)
{
    if (ctx == NULL || !ctx->inited || fb == NULL) {
        return DAL_ERR_INVALID;
    }
    if (index >= ctx->fb_count) {
        return DAL_ERR_INVALID;
    }
    *fb = ctx->fb[index];
    return DAL_OK;
}

/* ================================================================
 *  背光（直接 PWM 路径，内联原 pal_mipi_dsi_set_backlight）
 * ================================================================ */

dal_err_t bsp_tc358762_set_backlight(bsp_tc358762_ctx_t *ctx, uint8_t percent)
{
    if (ctx == NULL || !ctx->bl_configured) {
        return DAL_ERR_INVALID;
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
        return dal_err_from_esp(ret);
    }
    return dal_err_from_esp(ledc_update_duty(ctx->bl_speed_mode,
                             (ledc_channel_t)ctx->bl_channel));
}
