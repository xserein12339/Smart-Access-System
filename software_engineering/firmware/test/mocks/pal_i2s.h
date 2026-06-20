/**
 * @file    pal_i2s.h
 * @brief   Mock pal_i2s.h — PAL I2S 接口（宿主机测试用）
 *
 * @details 拦截被测 BSP（audio）对真实 PAL I2S 的依赖。用 FFF fake。
 *          类型定义与真实 pal_i2s.h 一致。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef MOCK_PAL_I2S_H
#define MOCK_PAL_I2S_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "fff.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *pal_i2s_handle_t;

typedef enum {
    PAL_I2S_ROLE_MASTER = 0,
    PAL_I2S_ROLE_SLAVE  = 1,
} pal_i2s_role_t;

typedef enum {
    PAL_I2S_DIR_TX    = 0,
    PAL_I2S_DIR_RX    = 1,
    PAL_I2S_DIR_TX_RX = 2,
} pal_i2s_dir_t;

typedef enum {
    PAL_I2S_SLOT_MONO   = 0,
    PAL_I2S_SLOT_STEREO = 1,
} pal_i2s_slot_mode_t;

typedef enum {
    PAL_I2S_DATA_8BIT  = 8,
    PAL_I2S_DATA_16BIT = 16,
    PAL_I2S_DATA_24BIT = 24,
    PAL_I2S_DATA_32BIT = 32,
} pal_i2s_data_bit_width_t;

typedef struct {
    int                      port;
    pal_i2s_role_t           role;
    pal_i2s_dir_t            dir;
    int                      mclk_pin;
    int                      bclk_pin;
    int                      ws_pin;
    int                      dout_pin;
    int                      din_pin;
    uint32_t                 sample_rate_hz;
    pal_i2s_data_bit_width_t data_bit_width;
    pal_i2s_slot_mode_t      slot_mode;
    int                      dma_desc_num;
    int                      dma_frame_num;
    int                      intr_priority;
} pal_i2s_config_t;

/* ================================================================
 *  FFF Fake 函数
 * ================================================================ */
#ifdef FFF_MOCK_DEFINITIONS
FAKE_VALUE_FUNC2(int, pal_i2s_init, pal_i2s_handle_t *, const pal_i2s_config_t *);
FAKE_VALUE_FUNC1(int, pal_i2s_deinit, pal_i2s_handle_t);
FAKE_VALUE_FUNC1(int, pal_i2s_enable, pal_i2s_handle_t);
FAKE_VALUE_FUNC1(int, pal_i2s_disable, pal_i2s_handle_t);
FAKE_VALUE_FUNC5(int, pal_i2s_write, pal_i2s_handle_t, const void *, size_t, uint32_t, size_t *);
FAKE_VALUE_FUNC5(int, pal_i2s_read, pal_i2s_handle_t, void *, size_t, uint32_t, size_t *);
#else
DECLARE_FAKE_VALUE_FUNC2(int, pal_i2s_init, pal_i2s_handle_t *, const pal_i2s_config_t *);
DECLARE_FAKE_VALUE_FUNC1(int, pal_i2s_deinit, pal_i2s_handle_t);
DECLARE_FAKE_VALUE_FUNC1(int, pal_i2s_enable, pal_i2s_handle_t);
DECLARE_FAKE_VALUE_FUNC1(int, pal_i2s_disable, pal_i2s_handle_t);
DECLARE_FAKE_VALUE_FUNC5(int, pal_i2s_write, pal_i2s_handle_t, const void *, size_t, uint32_t, size_t *);
DECLARE_FAKE_VALUE_FUNC5(int, pal_i2s_read, pal_i2s_handle_t, void *, size_t, uint32_t, size_t *);
#endif /* FFF_MOCK_DEFINITIONS */

#ifdef __cplusplus
}
#endif
#endif /* MOCK_PAL_I2S_H */
