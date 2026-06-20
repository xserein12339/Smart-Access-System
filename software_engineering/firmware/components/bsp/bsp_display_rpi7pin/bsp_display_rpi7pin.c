/**
 * @file    bsp_display_rpi7pin.c
 * @brief   RPI 7 寸屏 BSP 组装器 — 聚合 TC358762 + ATTINY88，自注册到 DAL
 *
 * @details 多芯片组装范式（架构文档 3.3）：
 *          - 聚合上下文 bsp_rpi7pin_agg_ctx_t 封装 tc358762(显示) 与
 *            attiny88(背光) 子芯片句柄。
 *          - 实现 dal_display_ops_t，将 DAL 接口调用分发到对应子芯片：
 *              init/fill/draw_bitmap/get_fb → tc358762
 *              set_backlight                → attiny88
 *          - bsp_display_rpi7pin_init() 初始化子芯片并以 "rpi7pin" 自注册到 DAL。
 *
 * @author  xLumina
 * @version 1.0
 */
#include "bsp_display_rpi7pin.h"
#include "dal_display.h"
#include "bsp_display_tc358762.h"
#include "bsp_display_attiny88.h"
#include "bsp_config.h"
#include "pal_i2c.h"
#include "osal_task.h"
#include "board_v1.h"

/* ====== 聚合上下文：封装所有子芯片句柄 ====== */
typedef struct {
    bsp_tc358762_ctx_t bridge;       /**< DSI 桥接 + 显示 */
    bsp_attiny88_ctx_t backlight;    /**< 背光 MCU */
    bool               use_attiny88; /**< 是否用 ATTINY88 控制背光 */
    uint16_t           width;
    uint16_t           height;
} bsp_rpi7pin_agg_ctx_t;

/* ====== 适配层：DAL 接口分发到子芯片 ====== */

static dal_err_t rpi7pin_init(void *ctx, const dal_display_config_t *cfg)
{
    bsp_rpi7pin_agg_ctx_t *agg = (bsp_rpi7pin_agg_ctx_t *)ctx;
    if (cfg == NULL) {
        return DAL_ERR_INVALID;
    }

    /* TC358762 在 bsp_display_rpi7pin_init() 已初始化，此处仅同步尺寸 */
    agg->width  = cfg->width;
    agg->height = cfg->height;

    /* 应用初始背光 */
    if (agg->use_attiny88) {
        return bsp_attiny88_set_backlight(&agg->backlight, cfg->brightness);
    }
    return DAL_OK;
}

static dal_err_t rpi7pin_fill(void *ctx, uint16_t x, uint16_t y,
                              uint16_t w, uint16_t h, uint16_t color)
{
    bsp_rpi7pin_agg_ctx_t *agg = (bsp_rpi7pin_agg_ctx_t *)ctx;
    return bsp_tc358762_fill(&agg->bridge, x, y, w, h, color);
}

static dal_err_t rpi7pin_draw_bitmap(void *ctx, uint16_t x, uint16_t y,
                                     uint16_t w, uint16_t h, const void *data)
{
    bsp_rpi7pin_agg_ctx_t *agg = (bsp_rpi7pin_agg_ctx_t *)ctx;
    return bsp_tc358762_draw_bitmap(&agg->bridge, x, y, w, h, data);
}

static dal_err_t rpi7pin_set_bl(void *ctx, uint8_t percent)
{
    bsp_rpi7pin_agg_ctx_t *agg = (bsp_rpi7pin_agg_ctx_t *)ctx;
    if (agg->use_attiny88) {
        return bsp_attiny88_set_backlight(&agg->backlight, percent);
    }
    /* 无 ATTINY88 时无可调背光 */
    return DAL_ERR_UNSUPPORTED;
}

static dal_err_t rpi7pin_get_fb(void *ctx, void **fb)
{
    bsp_rpi7pin_agg_ctx_t *agg = (bsp_rpi7pin_agg_ctx_t *)ctx;
    return bsp_tc358762_get_fb(&agg->bridge, fb);
}

static const dal_display_ops_t s_rpi7pin_ops = {
    .init          = rpi7pin_init,
    .fill          = rpi7pin_fill,
    .draw_bitmap   = rpi7pin_draw_bitmap,
    .set_backlight = rpi7pin_set_bl,
    .get_fb        = rpi7pin_get_fb,
};

/* ====== 对外初始化入口（自注册）====== */
static bsp_rpi7pin_agg_ctx_t s_agg_ctx;

dal_err_t bsp_display_rpi7pin_init(void)
{
    dal_err_t ret;

    s_agg_ctx.use_attiny88 = BOARD_DISPLAY_USE_ATTINY88;
    s_agg_ctx.width  = BOARD_DISPLAY_H_RES;
    s_agg_ctx.height = BOARD_DISPLAY_V_RES;

    /* 1. 初始化 ATTINY88 背光/电源 MCU（在共享 I2C 总线上） */
    if (s_agg_ctx.use_attiny88) {
        pal_i2c_bus_handle_t bus = (pal_i2c_bus_handle_t)board_i2c_get_bus();
        if (bus == NULL) {
            return DAL_ERR_STATE;   /* 共享 I2C 总线未就绪 */
        }
        ret = bsp_attiny88_init(&s_agg_ctx.backlight, bus);
        if (ret != DAL_OK) {
            return ret;
        }
    }

    /* 2. 初始化 DSI 主机 + DPI 面板 + 帧缓冲 */
    ret = bsp_tc358762_init(&s_agg_ctx.bridge);
    if (ret != DAL_OK) {
        if (s_agg_ctx.use_attiny88) {
            bsp_attiny88_deinit(&s_agg_ctx.backlight);
        }
        return ret;
    }

    /* 3. ATTINY88 上电序列：开主电源，复位保持 */
    if (s_agg_ctx.use_attiny88) {
        ret = bsp_attiny88_power_on(&s_agg_ctx.backlight);
        if (ret != DAL_OK) {
            bsp_tc358762_deinit(&s_agg_ctx.bridge);
            bsp_attiny88_deinit(&s_agg_ctx.backlight);
            return ret;
        }

        /* 4. 释放桥/LCD/触控复位（DSI 已就绪） */
        ret = bsp_attiny88_release_reset(&s_agg_ctx.backlight);
        if (ret != DAL_OK) {
            bsp_tc358762_deinit(&s_agg_ctx.bridge);
            bsp_attiny88_deinit(&s_agg_ctx.backlight);
            return ret;
        }
    }

    /* 5. 配置 TC358762 桥寄存器序列并启动 PPI/DSI */
    ret = bsp_tc358762_config_bridge(&s_agg_ctx.bridge);
    if (ret != DAL_OK) {
        bsp_tc358762_deinit(&s_agg_ctx.bridge);
        if (s_agg_ctx.use_attiny88) {
            bsp_attiny88_deinit(&s_agg_ctx.backlight);
        }
        return ret;
    }

    /* 6. 点亮背光 */
    if (s_agg_ctx.use_attiny88) {
        bsp_attiny88_set_backlight(&s_agg_ctx.backlight,
                                   BOARD_DISPLAY_DEFAULT_BRIGHTNESS);
    }

    /* 7. 自注册到 DAL */
    return dal_display_register("rpi7pin", &s_rpi7pin_ops, &s_agg_ctx);
}
