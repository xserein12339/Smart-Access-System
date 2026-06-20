/**
 * @file    fff_globals.c
 * @brief   FFF 全局定义 — 所有 FAKE_* 函数体在此编译
 *
 * @details 一个测试工程中只能有一个 .c 文件定义 FFF_MOCK_DEFINITIONS。
 *          包含本文件即得到所有 mock 函数的定义。各 mock 头在此展开
 *          FAKE_* 宏生成函数体与全局变量。
 *
 * @author  xLumina
 * @version 1.0
 */

#define FFF_MOCK_DEFINITIONS
#include "fff.h"
DEFINE_FFF_GLOBALS;

/* 按需包含各模块 mock 头，Fake 函数体在此编译 */
#include "pal_gpio.h"
#include "pal_i2c.h"
#include "pal_mipi_dsi.h"
#include "pal_i2s.h"
#include "osal_queue.h"
#include "osal_semaphore.h"
#include "osal_timer.h"
