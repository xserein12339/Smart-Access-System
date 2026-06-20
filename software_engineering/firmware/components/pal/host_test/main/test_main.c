/**
 * @file    test_main.c
 * @brief   PAL host_test Unity Fixture 入口
 *
 * 汇总各测试组的 group runner，提供 main() 调用 UnityMain。
 */

#include "unity.h"
#include "unity_fixture.h"

/* 由各测试文件定义的 group runner */
extern void runner_pal_ldo(void);
extern void runner_pal_wdt(void);
extern void runner_pal_gpio(void);
extern void runner_pal_spi(void);
extern void runner_pal_uart(void);
extern void runner_pal_ledc(void);
extern void runner_pal_i2s(void);
extern void runner_pal_i2c(void);

void run_all_tests(void)
{
    runner_pal_ldo();
    runner_pal_wdt();
    runner_pal_gpio();
    runner_pal_spi();
    runner_pal_uart();
    runner_pal_ledc();
    runner_pal_i2s();
    runner_pal_i2c();
}

int main(int argc, char **argv)
{
    return UnityMain(argc, (const char **)argv, run_all_tests);
}
