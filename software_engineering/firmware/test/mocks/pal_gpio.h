/**
 * @file    pal_gpio.h
 * @brief   Mock pal_gpio.h — PAL GPIO 接口（宿主机测试用）
 *
 * @details 拦截被测 BSP 代码对真实 PAL GPIO 的依赖。仅 mock bsp_relay
 *          实际使用的接口（set_direction/write/read），用 FFF 生成
 *          可控假函数。
 *
 *          包含规则：
 *          - 普通 .c：仅 extern 声明（DECLARE_FAKE_*）
 *          - 定义 FFF_MOCK_DEFINITIONS 后包含：展开函数体（FAKE_*），
 *            仅 fff_globals.c 应定义此宏。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef MOCK_PAL_GPIO_H
#define MOCK_PAL_GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "fff.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- PAL GPIO 类型定义（与真实 pal_gpio.h 一致，最小子集）---- */

typedef enum {
    PAL_GPIO_DIR_INPUT         = 0,
    PAL_GPIO_DIR_OUTPUT        = 1,
    PAL_GPIO_DIR_INPUT_OUTPUT  = 2,
} pal_gpio_dir_t;

typedef enum {
    PAL_GPIO_PULL_NONE = 0,
    PAL_GPIO_PULL_UP   = 1,
    PAL_GPIO_PULL_DOWN = 2,
} pal_gpio_pull_t;

/* ================================================================
 *  FFF Fake 函数 — 条件定义 / 声明
 * ================================================================ */

#ifdef FFF_MOCK_DEFINITIONS
FAKE_VALUE_FUNC2(int, pal_gpio_set_direction, int, pal_gpio_dir_t);
FAKE_VALUE_FUNC2(int, pal_gpio_write, int, int);
FAKE_VALUE_FUNC1(int, pal_gpio_read, int);
#else
DECLARE_FAKE_VALUE_FUNC2(int, pal_gpio_set_direction, int, pal_gpio_dir_t);
DECLARE_FAKE_VALUE_FUNC2(int, pal_gpio_write, int, int);
DECLARE_FAKE_VALUE_FUNC1(int, pal_gpio_read, int);
#endif /* FFF_MOCK_DEFINITIONS */

#ifdef __cplusplus
}
#endif
#endif /* MOCK_PAL_GPIO_H */
