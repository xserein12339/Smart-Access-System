/**
 * @file    bsp_display_tc358762.c
 * @brief   TC358762 DSI 桥接芯片子驱动实现
 *
 * @details 组装 PAL mipi_dsi 配置（参数来自 bsp_config.h），初始化 DSI 主机；
 *          通过 DSI Generic Long Write 写桥寄存器序列配置 TC358762；
 *          提供 fill/draw_bitmap/get_fb 绘图操作。
 *          所有 PAL 返回码经 dal_err_from_pal 翻译为 dal_err_t。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "bsp_display_tc358762.h"
#include "dal_pal_err.h"
#include "bsp_config.h"
#include "pal_log.h"

/* ---- 内部：经 DSI Generic Long Write 写 32-bit 值到 16-bit 寄存器 ----
 * payload[6] = { reg_lo, reg_hi, val_b0, val_b1, val_b2, val_b3 }
 */
static dal_err_t tc358762_write_reg(bsp_tc358762_ctx_t *ctx,
                                    uint16_t addr, uint32_t val)
{
    if (ctx == NULL || ctx->dsi == NULL) {
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
    return dal_err_from_pal(pal_mipi_dsi_send_generic_write(ctx->dsi, ctx->vc, payload, 6));
}

/* ---- 组装 PAL DSI 配置（参数来自 bsp_config.h）---- */
static void tc358762_build_dsi_cfg(pal_mipi_dsi_config_t *cfg)
{
    cfg->dsi_host            = BOARD_DISPLAY_DSI_HOST;
    cfg->num_data_lanes      = BOARD_DISPLAY_DSI_NUM_DATA_LANES;
    cfg->lane_bit_rate_mbps  = BOARD_DISPLAY_DSI_LANE_BIT_RATE_MBPS;
    cfg->virtual_channel     = BOARD_DISPLAY_DSI_VIRTUAL_CHANNEL;
    cfg->h_res               = BOARD_DISPLAY_H_RES;
    cfg->v_res               = BOARD_DISPLAY_V_RES;
    cfg->pixel_format        = (pal_mipi_dsi_color_fmt_t)BOARD_DISPLAY_PIXEL_FORMAT;
    cfg->in_color_format     = (pal_mipi_dsi_in_color_fmt_t)BOARD_DISPLAY_IN_COLOR_FORMAT;
    cfg->dpi_clock_freq_mhz  = BOARD_DISPLAY_DPI_CLOCK_FREQ_MHZ;
    cfg->num_fbs             = BOARD_DISPLAY_NUM_FBS;
    cfg->use_dma2d           = false;

    cfg->timing.h_res               = BOARD_DISPLAY_H_RES;
    cfg->timing.v_res               = BOARD_DISPLAY_V_RES;
    cfg->timing.hsync_pulse_width   = BOARD_DISPLAY_HSYNC_PULSE_WIDTH;
    cfg->timing.hsync_back_porch    = BOARD_DISPLAY_HSYNC_BACK_PORCH;
    cfg->timing.hsync_front_porch   = BOARD_DISPLAY_HSYNC_FRONT_PORCH;
    cfg->timing.vsync_pulse_width   = BOARD_DISPLAY_VSYNC_PULSE_WIDTH;
    cfg->timing.vsync_back_porch    = BOARD_DISPLAY_VSYNC_BACK_PORCH;
    cfg->timing.vsync_front_porch   = BOARD_DISPLAY_VSYNC_FRONT_PORCH;
    cfg->timing.hsync_polarity      = BOARD_DISPLAY_HSYNC_POLARITY;
    cfg->timing.vsync_polarity      = BOARD_DISPLAY_VSYNC_POLARITY;

    /* 背光由 ATTINY88 控制，DSI 侧不启用 LEDC 背光 */
    cfg->bl_enabled      = false;
    cfg->bl_gpio         = BOARD_DISPLAY_BL_GPIO;
    cfg->bl_ledc_channel = BOARD_DISPLAY_BL_LEDC_CHANNEL;
    cfg->bl_ledc_timer   = BOARD_DISPLAY_BL_LEDC_TIMER;
    cfg->bl_freq_hz      = BOARD_DISPLAY_BL_FREQ_HZ;
}

dal_err_t bsp_tc358762_init(bsp_tc358762_ctx_t *ctx)
{
    if (ctx == NULL) {
        return DAL_ERR_INVALID;
    }

    pal_mipi_dsi_config_t cfg = {0};
    tc358762_build_dsi_cfg(&cfg);

    int ret = pal_mipi_dsi_init(&ctx->dsi, &cfg);
    if (ret != 0) {
        return dal_err_from_pal(ret);
    }

    ctx->vc     = (uint8_t)BOARD_DISPLAY_DSI_VIRTUAL_CHANNEL;
    ctx->width  = BOARD_DISPLAY_H_RES;
    ctx->height = BOARD_DISPLAY_V_RES;
    ctx->inited = true;

    PAL_LOGI("TC358762", "DSI host initialized (%ux%u)",
             ctx->width, ctx->height);
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

    PAL_LOGI("TC358762", "bridge configured & started");
    return DAL_OK;
}

dal_err_t bsp_tc358762_deinit(bsp_tc358762_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }
    /* 停桥接 */
    tc358762_write_reg(ctx, TC358762_REG_SYSCTRL, 0x0400);

    dal_err_t ret = dal_err_from_pal(pal_mipi_dsi_deinit(ctx->dsi));
    ctx->dsi    = NULL;
    ctx->inited = false;
    return ret;
}

dal_err_t bsp_tc358762_fill(bsp_tc358762_ctx_t *ctx,
                             uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h, uint16_t color)
{
    if (ctx == NULL || !ctx->inited) {
        return DAL_ERR_INVALID;
    }
    return dal_err_from_pal(pal_mipi_dsi_fill(ctx->dsi, x, y, w, h, (uint32_t)color));
}

dal_err_t bsp_tc358762_draw_bitmap(bsp_tc358762_ctx_t *ctx,
                                   uint16_t x, uint16_t y,
                                   uint16_t w, uint16_t h, const void *data)
{
    if (ctx == NULL || !ctx->inited || data == NULL) {
        return DAL_ERR_INVALID;
    }
    return dal_err_from_pal(pal_mipi_dsi_draw_bitmap(ctx->dsi, x, y, w, h, data));
}

dal_err_t bsp_tc358762_get_fb(bsp_tc358762_ctx_t *ctx, void **fb)
{
    if (ctx == NULL || !ctx->inited || fb == NULL) {
        return DAL_ERR_INVALID;
    }
    void *fb0 = NULL;
    void *fb1 = NULL;
    int ret = pal_mipi_dsi_get_fb(ctx->dsi, &fb0, &fb1);
    if (ret != 0) {
        return dal_err_from_pal(ret);
    }
    *fb = fb0;
    return DAL_OK;
}
