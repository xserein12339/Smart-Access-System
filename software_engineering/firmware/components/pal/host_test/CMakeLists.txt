# PAL host_test 项目根 CMakeLists
# 独立的 ESP-IDF linux target 项目，用于在 PC 上对 PAL 模块做 mock 单元测试
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(COMPONENTS main)

# freertos / driver: 用 IDF 预生成的 mock（覆盖真实组件）
#   - freertos: task.h/queue.h/event_groups.h（pal_wdt 等依赖）
#   - driver:   gpio.h/spi_master.h/spi_common.h（pal_gpio/pal_spi 依赖）
# esp_hw_support / esp_system / esp_driver_uart / esp_driver_ledc /
# esp_driver_i2s / esp_driver_i2c: 本项目自定义 mock 组件
list(APPEND EXTRA_COMPONENT_DIRS
    "$ENV{IDF_PATH}/tools/mocks/freertos/"
    "$ENV{IDF_PATH}/tools/mocks/driver/"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/esp_hw_support"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/esp_system"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/esp_driver_uart"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/esp_driver_ledc"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/esp_driver_i2s"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/esp_driver_i2c"
)

project(pal_host_test)
