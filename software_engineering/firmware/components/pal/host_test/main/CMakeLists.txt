# main 组件：注册测试源文件 + 被测 SUT（各 pal_xxx.c 原样编译）
set(PAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../")

idf_component_register(
    SRCS
        "test_main.c"
        "test_pal_ldo.c"
        "test_pal_wdt.c"
        "test_pal_gpio.c"
        "test_pal_spi.c"
        "test_pal_uart.c"
        "test_pal_ledc.c"
        "test_pal_i2s.c"
        "test_pal_i2c.c"
        "${PAL_DIR}/pal_ldo/pal_ldo.c"
        "${PAL_DIR}/pal_wdt/pal_wdt.c"
        "${PAL_DIR}/pal_gpio/pal_gpio.c"
        "${PAL_DIR}/pal_spi/pal_spi.c"
        "${PAL_DIR}/pal_uart/pal_uart.c"
        "${PAL_DIR}/pal_ledc/pal_ledc.c"
        "${PAL_DIR}/pal_i2s/pal_i2s.c"
        "${PAL_DIR}/pal_i2c/pal_i2c.c"
    INCLUDE_DIRS
        "${PAL_DIR}/pal_ldo/include"
        "${PAL_DIR}/pal_wdt/include"
        "${PAL_DIR}/pal_gpio/include"
        "${PAL_DIR}/pal_spi/include"
        "${PAL_DIR}/pal_uart/include"
        "${PAL_DIR}/pal_ledc/include"
        "${PAL_DIR}/pal_i2s/include"
        "${PAL_DIR}/pal_i2c/include"
    REQUIRES
        unity
        cmock
        freertos
        driver
        esp_driver_uart
        esp_driver_ledc
        esp_driver_i2s
        esp_driver_i2c
)

# SUT（pal_uart/ledc/i2s/i2c.c）编译时需要 SOC caps 宏
include("${CMAKE_CURRENT_SOURCE_DIR}/../soc_caps_defs.cmake")
apply_soc_caps(${COMPONENT_LIB})
