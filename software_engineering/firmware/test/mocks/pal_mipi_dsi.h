/**
 * @file    pal_mipi_dsi.h
 * @brief   Mock pal_mipi_dsi.h — PAL MIPI DSI 接口（宿主机测试用）
 *
 * @details 拦截被测 BSP（tc358762）对真实 PAL DSI 的依赖。用 FFF fake。
 *          类型定义与真实 pal_mipi_dsi.h 一致（tc358762 填充 config 结构体）。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef MOCK_PAL_MIPI_DSI_H
#define MOCK_PAL_MIPI_DSI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "fff.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *pal_mipi_dsi_handle_t;

typedef enum {
    PAL_DSI_COLOR_RGB565 = 0,
    PAL_DSI_COLOR_RGB666 = 1,
    PAL_DSI_COLOR_RGB888 = 2,
} pal_mipi_dsi_color_fmt_t;

typedef enum {
    PAL_DSI_IN_COLOR_RGB565 = 0,
    PAL_DSI_IN_COLOR_RGB888 = 1,
} pal_mipi_dsi_in_color_fmt_t;

typedef struct {
    uint16_t h_res;
    uint16_t v_res;
    uint16_t hsync_pulse_width;
    uint16_t hsync_back_porch;
    uint16_t hsync_front_porch;
    uint16_t vsync_pulse_width;
    uint16_t vsync_back_porch;
    uint16_t vsync_front_porch;
    uint16_t hsync_polarity;
    uint16_t vsync_polarity;
} pal_mipi_dsi_timing_t;

typedef struct {
    int                          dsi_host;
    uint8_t                      num_data_lanes;
    int                          lane_bit_rate_mbps;
    int                          virtual_channel;
    uint16_t                     h_res;
    uint16_t                     v_res;
    pal_mipi_dsi_color_fmt_t     pixel_format;
    pal_mipi_dsi_in_color_fmt_t  in_color_format;
    pal_mipi_dsi_timing_t        timing;
    float                        dpi_clock_freq_mhz;
    int                          num_fbs;
    bool                         use_dma2d;
    bool                         bl_enabled;
    int                          bl_gpio;
    int                          bl_ledc_channel;
    int                          bl_ledc_timer;
    uint32_t                     bl_freq_hz;
} pal_mipi_dsi_config_t;

/* ================================================================
 *  FFF Fake 函数
 * ================================================================ */
#ifdef FFF_MOCK_DEFINITIONS
FAKE_VALUE_FUNC2(int, pal_mipi_dsi_init, pal_mipi_dsi_handle_t *, const pal_mipi_dsi_config_t *);
FAKE_VALUE_FUNC1(int, pal_mipi_dsi_deinit, pal_mipi_dsi_handle_t);
FAKE_VALUE_FUNC1(int, pal_mipi_dsi_display_on, pal_mipi_dsi_handle_t);
FAKE_VALUE_FUNC1(int, pal_mipi_dsi_display_off, pal_mipi_dsi_handle_t);
FAKE_VALUE_FUNC6(int, pal_mipi_dsi_draw_bitmap, pal_mipi_dsi_handle_t, uint16_t, uint16_t, uint16_t, uint16_t, const void *);
FAKE_VALUE_FUNC6(int, pal_mipi_dsi_fill, pal_mipi_dsi_handle_t, uint16_t, uint16_t, uint16_t, uint16_t, uint32_t);
FAKE_VALUE_FUNC3(int, pal_mipi_dsi_get_fb, pal_mipi_dsi_handle_t, void **, void **);
FAKE_VALUE_FUNC2(int, pal_mipi_dsi_set_backlight, pal_mipi_dsi_handle_t, uint8_t);
FAKE_VALUE_FUNC4(int, pal_mipi_dsi_send_generic_write, pal_mipi_dsi_handle_t, uint8_t, const uint8_t *, size_t);
#else
DECLARE_FAKE_VALUE_FUNC2(int, pal_mipi_dsi_init, pal_mipi_dsi_handle_t *, const pal_mipi_dsi_config_t *);
DECLARE_FAKE_VALUE_FUNC1(int, pal_mipi_dsi_deinit, pal_mipi_dsi_handle_t);
DECLARE_FAKE_VALUE_FUNC1(int, pal_mipi_dsi_display_on, pal_mipi_dsi_handle_t);
DECLARE_FAKE_VALUE_FUNC1(int, pal_mipi_dsi_display_off, pal_mipi_dsi_handle_t);
DECLARE_FAKE_VALUE_FUNC6(int, pal_mipi_dsi_draw_bitmap, pal_mipi_dsi_handle_t, uint16_t, uint16_t, uint16_t, uint16_t, const void *);
DECLARE_FAKE_VALUE_FUNC6(int, pal_mipi_dsi_fill, pal_mipi_dsi_handle_t, uint16_t, uint16_t, uint16_t, uint16_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC3(int, pal_mipi_dsi_get_fb, pal_mipi_dsi_handle_t, void **, void **);
DECLARE_FAKE_VALUE_FUNC2(int, pal_mipi_dsi_set_backlight, pal_mipi_dsi_handle_t, uint8_t);
DECLARE_FAKE_VALUE_FUNC4(int, pal_mipi_dsi_send_generic_write, pal_mipi_dsi_handle_t, uint8_t, const uint8_t *, size_t);
#endif /* FFF_MOCK_DEFINITIONS */

#ifdef __cplusplus
}
#endif
#endif /* MOCK_PAL_MIPI_DSI_H */
