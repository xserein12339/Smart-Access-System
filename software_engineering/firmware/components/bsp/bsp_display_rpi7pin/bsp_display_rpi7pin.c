/**
 * @file    bsp_display_rpi7pin.c
 * @brief   RPI 7 寸屏 BSP 聚合层 — 聚合 TC358762 + ATTINY88，create 模式
 *
 * @details 多芯片聚合范式（架构文档 3.3）：
 *          - 聚合上下文 bsp_rpi7pin_agg_ctx_t 封装 tc358762(显示) 与
 *            attiny88(背光) 子芯片上下文。
 *          - 实现 dal_display_ops_t，将 DAL 接口调用分发到对应子芯片：
 *              init/fill/draw_bitmap/get_fb/deinit → tc358762
 *              set_backlight → attiny88（或 tc358762 直接 PWM 路径）
 *          - bsp_display_rpi7pin_create() 仅做 ctx 字段初值（memset ctx），
 *            返回静态 ops（ctx 编译期已注入 ops->ctx），零硬件副作用。硬件
 *            初始化封装在 ops->init()，编排两阶段上电序列，由上层按需触发。
 *
 *          子驱动 TC358762 / ATTINY88 已改原生 ESP-IDF 调用（esp_lcd /
 *          i2c_master / esp_ldo / FreeRTOS），不再依赖 pal 层。
 *
 * @author  xLumina
 * @version 2.0
 */
#include "bsp_display_rpi7pin.h"
#include "bsp_display_tc358762.h"
#include "bsp_display_attiny88.h"
#include "board_v1_config.h"
#include "esp_log.h"
#include "board_v1.h"
#include "driver/i2c_master.h"       /* i2c_master_bus_handle_t */

#include <string.h>

static const char *TAG = "RPI7PIN";

/* ================================================================
 *  聚合上下文：封装所有子芯片句柄
 * ================================================================ */
typedef struct {
    bsp_tc358762_ctx_t bridge;       /**< DSI 桥接 + 显示 */
    bsp_attiny88_ctx_t backlight;    /**< 背光 MCU */
    bool               use_attiny88; /**< 是否用 ATTINY88 控制背光 */
    uint16_t           width;        /**< 水平分辨率 */
    uint16_t           height;       /**< 垂直分辨率 */
} bsp_rpi7pin_agg_ctx_t;

/* ================================================================
 *  适配层：DAL 接口分发到子芯片
 * ================================================================ */

/**
 * @brief 初始化显示设备（编排两阶段上电序列）
 *
 * @details 顺序严格（保留原 bsp_display_rpi7pin_init 逻辑）：
 *          (1) ATTINY88 init（共享 I2C）
 *          (2) TC358762 init（DSI bus + DPI panel + 帧缓冲 + LDO）
 *          (3) ATTINY88 power_on
 *          (4) ATTINY88 release_reset
 *          (5) TC358762 config_bridge（DSI Generic Write 14 寄存器 + 启动 PPI/DSI）
 *          (6) ATTINY88 set_backlight（初始亮度）
 */
static dal_err_t rpi7pin_init(void *ctx, const dal_display_config_t *cfg)
{
    bsp_rpi7pin_agg_ctx_t *agg = (bsp_rpi7pin_agg_ctx_t *)ctx;
    if (agg == NULL) {
        return DAL_ERR_INVALID;
    }

    uint8_t brightness = (cfg != NULL) ? cfg->brightness
                                       : BOARD_DISPLAY_DEFAULT_BRIGHTNESS;

    agg->use_attiny88 = BOARD_DISPLAY_USE_ATTINY88;
    agg->width  = BOARD_DISPLAY_H_RES;
    agg->height = BOARD_DISPLAY_V_RES;

    dal_err_t ret;

    /* 1. 初始化 ATTINY88 背光/电源 MCU（在共享 I2C 总线上） */
    if (agg->use_attiny88) {
        i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)board_i2c_get_bus();
        if (bus == NULL) {
            return DAL_ERR_STATE;   /* 共享 I2C 总线未就绪 */
        }
        ret = bsp_attiny88_init(&agg->backlight, bus);
        if (ret != DAL_OK) {
            return ret;
        }
    }

    /* 2. 初始化 DSI 主机 + DPI 面板 + 帧缓冲 */
    ret = bsp_tc358762_init(&agg->bridge);
    if (ret != DAL_OK) {
        if (agg->use_attiny88) {
            bsp_attiny88_deinit(&agg->backlight);
        }
        return ret;
    }

    /* 3. ATTINY88 上电序列：开主电源，复位保持 */
    if (agg->use_attiny88) {
        ret = bsp_attiny88_power_on(&agg->backlight);
        if (ret != DAL_OK) {
            goto fail;
        }

        /* 4. 释放桥/LCD/触控复位（DSI 已就绪） */
        ret = bsp_attiny88_release_reset(&agg->backlight);
        if (ret != DAL_OK) {
            goto fail;
        }
    }

    /* 5. 配置 TC358762 桥寄存器序列并启动 PPI/DSI */
    ret = bsp_tc358762_config_bridge(&agg->bridge);
    if (ret != DAL_OK) {
        goto fail;
    }

    /* 6. 点亮背光（初始亮度） */
    if (agg->use_attiny88) {
        bsp_attiny88_set_backlight(&agg->backlight, brightness);
    } else {
        bsp_tc358762_set_backlight(&agg->bridge, brightness);
    }

    ESP_LOGI(TAG, "display initialized (%ux%u)", agg->width, agg->height);
    return DAL_OK;

fail:
    bsp_tc358762_deinit(&agg->bridge);
    if (agg->use_attiny88) {
        bsp_attiny88_deinit(&agg->backlight);
    }
    return ret;
}

static dal_err_t rpi7pin_fill(void *ctx, uint16_t x, uint16_t y,
                              uint16_t w, uint16_t h, uint16_t color)
{
    bsp_rpi7pin_agg_ctx_t *agg = (bsp_rpi7pin_agg_ctx_t *)ctx;
    if (agg == NULL) {
        return DAL_ERR_INVALID;
    }
    return bsp_tc358762_fill(&agg->bridge, x, y, w, h, color);
}

static dal_err_t rpi7pin_draw_bitmap(void *ctx, uint16_t x, uint16_t y,
                                     uint16_t w, uint16_t h, const void *data)
{
    bsp_rpi7pin_agg_ctx_t *agg = (bsp_rpi7pin_agg_ctx_t *)ctx;
    if (agg == NULL) {
        return DAL_ERR_INVALID;
    }
    return bsp_tc358762_draw_bitmap(&agg->bridge, x, y, w, h, data);
}

static dal_err_t rpi7pin_set_bl(void *ctx, uint8_t percent)
{
    bsp_rpi7pin_agg_ctx_t *agg = (bsp_rpi7pin_agg_ctx_t *)ctx;
    if (agg == NULL) {
        return DAL_ERR_INVALID;
    }
    if (agg->use_attiny88) {
        return bsp_attiny88_set_backlight(&agg->backlight, percent);
    }
    /* 无 ATTINY88 时走 TC358762 直接 PWM 背光路径 */
    return bsp_tc358762_set_backlight(&agg->bridge, percent);
}

static dal_err_t rpi7pin_get_fb(void *ctx, uint8_t index, void **fb)
{
    bsp_rpi7pin_agg_ctx_t *agg = (bsp_rpi7pin_agg_ctx_t *)ctx;
    if (agg == NULL || fb == NULL) {
        return DAL_ERR_INVALID;
    }
    return bsp_tc358762_get_fb(&agg->bridge, index, fb);
}

static dal_err_t rpi7pin_deinit(void *ctx)
{
    bsp_rpi7pin_agg_ctx_t *agg = (bsp_rpi7pin_agg_ctx_t *)ctx;
    if (agg == NULL) {
        return DAL_ERR_INVALID;
    }
    bsp_tc358762_deinit(&agg->bridge);
    if (agg->use_attiny88) {
        bsp_attiny88_deinit(&agg->backlight);
    }
    return DAL_OK;
}

/* ================================================================
 *  静态 ops + 聚合 ctx（单实例，ctx 编译期注入 ops->ctx）
 * ================================================================ */
static bsp_rpi7pin_agg_ctx_t s_ctx;

static dal_display_ops_t s_ops = {
    .init          = rpi7pin_init,
    .fill          = rpi7pin_fill,
    .draw_bitmap   = rpi7pin_draw_bitmap,
    .set_backlight = rpi7pin_set_bl,
    .get_fb        = rpi7pin_get_fb,
    .deinit        = rpi7pin_deinit,
    .ctx           = &s_ctx,
};

/* ================================================================
 *  对外 create 入口（仅绑定，不注册、不初始化硬件）
 * ================================================================ */
dal_display_ops_t *bsp_display_rpi7pin_create(void)
{
    /* 清零聚合上下文（含子芯片上下文），零硬件副作用；
     * ctx 已编译期注入 ops->ctx */
    memset(&s_ctx, 0, sizeof(s_ctx));

    return &s_ops;
}
