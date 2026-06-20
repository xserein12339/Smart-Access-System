/**
 * @file    clk_src_stubs.h
 * @brief   linux host_test 时钟源类型与默认值桩
 *
 * linux 的 soc/clk_tree_defs.h 仅定义 SPI 时钟源类型，缺少 UART/I2S/I2C/LEDC 的
 * soc_periph_*_clk_src_t 及其 DEFAULT/USE 枚举成员，导致 hal/uart_types.h 等头文件
 * typedef 失败、I2C_CLK_SRC_DEFAULT / LEDC_USE_*_CLK 等标识符未定义。
 * 本头通过 gcc -include 在编译单元开头强制注入，补全缺失的枚举类型与成员，
 * 不与原 clk_tree_defs.h 冲突（仅定义原文件没有的类型；SPI 相关由原文件提供）。
 */
#pragma once

/* UART 时钟源（uart_sclk_t 的底层类型，含 uart_config_t.source_clk 引用的 UART_SCLK_DEFAULT） */
typedef enum {
    UART_SCLK_DEFAULT = 0,
    UART_CLK_SRC_DEFAULT = 0,
} soc_periph_uart_clk_src_legacy_t;

/* I2S 时钟源（i2s_clock_src_t 的底层类型，含 I2S_STD_CLK_DEFAULT_CONFIG 引用的 DEFAULT） */
typedef enum {
    I2S_CLK_SRC_DEFAULT = 0,
} soc_periph_i2s_clk_src_t;

/* I2C 时钟源（i2c_clock_source_t 的底层类型，含 i2c_master.h 引用的 DEFAULT） */
typedef enum {
    I2C_CLK_SRC_DEFAULT = 0,
} soc_periph_i2c_clk_src_t;

/* LEDC 时钟源（ledc_clk_cfg_t 的底层类型，成员即 LEDC_AUTO_CLK / LEDC_USE_*_CLK，被 ledc_types.h 引用） */
typedef enum {
    LEDC_AUTO_CLK        = 0,
    LEDC_USE_RC_FAST_CLK = 1,
    LEDC_USE_APB_CLK     = 2,
    LEDC_USE_PLL_DIV_CLK = 3,
    LEDC_USE_XTAL_CLK    = 4,
} soc_periph_ledc_clk_src_legacy_t;
