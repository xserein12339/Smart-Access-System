cmake_minimum_required(VERSION 3.16)
project(face_identify_host_test LANGUAGES C)

# ---- 构建选项 ----
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -g -O0")

# 被测工程根目录（components/ 所在，即 firmware/ 本身）
set(FW_DIR ${CMAKE_SOURCE_DIR}/..)

# ---- 公共源 ----
set(UNITY_SRCS  ${CMAKE_SOURCE_DIR}/unity/unity.c)
set(FFF_SRCS    ${CMAKE_SOURCE_DIR}/fff_globals.c)

# ---- 公共 include 顺序：mocks 优先于真实头 ----
# 注意：mocks/ 必须在 components/.../include 之前，使 #include "pal_gpio.h"
# / "osal_mutex.h" / "esp_err.h" 命中 mock 而非真实头。
set(COMMON_INCLUDES
    ${CMAKE_SOURCE_DIR}/fff
    ${CMAKE_SOURCE_DIR}/unity
    ${CMAKE_SOURCE_DIR}/mocks
)

# ================================================================
#  dal_registry 测试
# ================================================================
add_executable(dal_registry_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${FW_DIR}/components/dal/common/dal_registry.c
    ${CMAKE_SOURCE_DIR}/test_dal_registry.c
)
target_include_directories(dal_registry_test PRIVATE
    ${COMMON_INCLUDES}
    ${FW_DIR}/components/dal/common/include
    ${FW_DIR}/components/osal/include
)

# ================================================================
#  dal_relay 管理测试
# ================================================================
add_executable(dal_relay_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${FW_DIR}/components/dal/dal_relay/dal_relay.c
    ${CMAKE_SOURCE_DIR}/test_dal_relay.c
)
target_include_directories(dal_relay_test PRIVATE
    ${COMMON_INCLUDES}
    ${FW_DIR}/components/dal/common/include
    ${FW_DIR}/components/dal/dal_relay/include
    ${FW_DIR}/components/osal/include
)

# ================================================================
#  bsp_relay 测试（ops + 错误码翻译，需 dal_relay register）
# ================================================================
add_executable(bsp_relay_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${FW_DIR}/components/dal/dal_relay/dal_relay.c
    ${FW_DIR}/components/bsp/bsp_relay/bsp_relay.c
    ${CMAKE_SOURCE_DIR}/test_bsp_relay.c
)
target_include_directories(bsp_relay_test PRIVATE
    ${COMMON_INCLUDES}
    ${FW_DIR}/components/dal/common/include
    ${FW_DIR}/components/dal/dal_relay/include
    ${FW_DIR}/components/bsp/bsp_relay/include
    ${FW_DIR}/components/osal/include
)

# ================================================================
#  attiny88 子驱动测试（I2C 电源/复位序列）
# ================================================================
add_executable(attiny88_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${FW_DIR}/components/bsp/bsp_display_rpi7pin/bsp_display_attiny88.c
    ${CMAKE_SOURCE_DIR}/test_attiny88.c
)
target_include_directories(attiny88_test PRIVATE
    ${COMMON_INCLUDES}
    ${FW_DIR}/components/dal/common/include
    ${FW_DIR}/components/bsp/bsp_display_rpi7pin
    ${FW_DIR}/components/bsp/bsp_config/include
    ${FW_DIR}/components/bsp/bsp_display_rpi7pin/include
    ${FW_DIR}/components/pal/pal_i2c/include
    ${FW_DIR}/components/osal/include
)

# ================================================================
#  tc358762 子驱动测试（DSI 桥寄存器序列）
# ================================================================
add_executable(tc358762_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${FW_DIR}/components/bsp/bsp_display_rpi7pin/bsp_display_tc358762.c
    ${CMAKE_SOURCE_DIR}/test_tc358762.c
)
target_include_directories(tc358762_test PRIVATE
    ${COMMON_INCLUDES}
    ${FW_DIR}/components/dal/common/include
    ${FW_DIR}/components/bsp/bsp_display_rpi7pin
    ${FW_DIR}/components/bsp/bsp_config/include
    ${FW_DIR}/components/pal/pal_mipi_dsi/include
)

# ================================================================
#  touch ft5406 测试（FT5406 协议解析）
# ================================================================
add_executable(touch_ft5406_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${FW_DIR}/components/dal/dal_touch/dal_touch.c
    ${FW_DIR}/components/dal/common/dal_registry.c
    ${FW_DIR}/components/bsp/bsp_touch_ft5406/bsp_touch_ft5406.c
    ${CMAKE_SOURCE_DIR}/test_touch_ft5406.c
)
target_include_directories(touch_ft5406_test PRIVATE
    ${COMMON_INCLUDES}
    ${FW_DIR}/components/dal/common/include
    ${FW_DIR}/components/dal/dal_touch/include
    ${FW_DIR}/components/bsp/bsp_touch_ft5406/include
    ${FW_DIR}/components/bsp/bsp_config/include
    ${FW_DIR}/components/bsp/board_v1/include
    ${FW_DIR}/components/pal/pal_i2c/include
    ${FW_DIR}/components/osal/include
)

# ================================================================
#  ES8311 codec 子驱动测试（寄存器序列）
# ================================================================
add_executable(audio_codec_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${FW_DIR}/components/bsp/bsp_audio_es8311/bsp_audio_es8311_codec.c
    ${CMAKE_SOURCE_DIR}/test_audio_codec.c
)
target_include_directories(audio_codec_test PRIVATE
    ${COMMON_INCLUDES}
    ${FW_DIR}/components/dal/common/include
    ${FW_DIR}/components/bsp/bsp_audio_es8311
    ${FW_DIR}/components/bsp/bsp_config/include
    ${FW_DIR}/components/pal/pal_i2c/include
    ${FW_DIR}/components/osal/include
)

# ================================================================
#  ES8311 音频组装器测试（I2S + GPIO + codec 编排）
# ================================================================
add_executable(audio_es8311_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${FW_DIR}/components/dal/dal_audio/dal_audio.c
    ${FW_DIR}/components/dal/common/dal_registry.c
    ${FW_DIR}/components/bsp/bsp_audio_es8311/bsp_audio_es8311.c
    ${FW_DIR}/components/bsp/bsp_audio_es8311/bsp_audio_es8311_codec.c
    ${CMAKE_SOURCE_DIR}/test_audio_es8311.c
)
target_include_directories(audio_es8311_test PRIVATE
    ${COMMON_INCLUDES}
    ${FW_DIR}/components/dal/common/include
    ${FW_DIR}/components/dal/dal_audio/include
    ${FW_DIR}/components/bsp/bsp_audio_es8311
    ${FW_DIR}/components/bsp/bsp_audio_es8311/include
    ${FW_DIR}/components/bsp/bsp_config/include
    ${FW_DIR}/components/bsp/board_v1/include
    ${FW_DIR}/components/pal/pal_i2c/include
    ${FW_DIR}/components/pal/pal_i2s/include
    ${FW_DIR}/components/pal/pal_gpio/include
    ${FW_DIR}/components/osal/include
)

# ================================================================
#  service_camera 测试（帧分发 + 引用计数共享）
# ================================================================
add_executable(service_camera_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${FW_DIR}/components/middleware/buffer_pool/buffer_pool_manager.c
    ${FW_DIR}/main/service/service_camera/service_camera.c
    ${CMAKE_SOURCE_DIR}/test_service_camera.c
)
target_include_directories(service_camera_test PRIVATE
    ${COMMON_INCLUDES}
    ${CMAKE_SOURCE_DIR}/mocks_svc
    ${FW_DIR}/components/dal/common/include
    ${FW_DIR}/components/dal/dal_camera/include
    ${FW_DIR}/components/middleware/buffer_pool/include
    ${FW_DIR}/components/middleware/event_bus/include
    ${FW_DIR}/main/service/service_camera/include
    ${FW_DIR}/components/osal/include
    ${FW_DIR}/components/pal/pal_log/include
)

# ================================================================
#  middleware buffer_pool 测试（引用计数缓冲池）
# ================================================================
add_executable(buffer_pool_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${FW_DIR}/components/middleware/buffer_pool/buffer_pool_manager.c
    ${CMAKE_SOURCE_DIR}/test_buffer_pool.c
)
target_include_directories(buffer_pool_test PRIVATE
    ${COMMON_INCLUDES}
    ${FW_DIR}/components/middleware/buffer_pool/include
    ${FW_DIR}/components/osal/include
)

# ================================================================
#  middleware event_bus 测试（发布/订阅逻辑）
# ================================================================
add_executable(event_bus_test
    ${UNITY_SRCS}
    ${FFF_SRCS}
    ${FW_DIR}/components/middleware/event_bus/mw_event_bus.c
    ${CMAKE_SOURCE_DIR}/test_event_bus.c
)
target_include_directories(event_bus_test PRIVATE
    ${COMMON_INCLUDES}
    ${FW_DIR}/components/dal/common/include
    ${FW_DIR}/components/middleware/event_bus/include
    ${FW_DIR}/components/osal/include
    ${FW_DIR}/components/pal/pal_log/include
)

# ================================================================
#  便捷目标：构建并运行全部测试
# ================================================================
add_custom_target(check
    COMMAND $<TARGET_FILE:dal_registry_test>
    COMMAND $<TARGET_FILE:dal_relay_test>
    COMMAND $<TARGET_FILE:bsp_relay_test>
    COMMAND $<TARGET_FILE:attiny88_test>
    COMMAND $<TARGET_FILE:tc358762_test>
    COMMAND $<TARGET_FILE:touch_ft5406_test>
    COMMAND $<TARGET_FILE:audio_codec_test>
    COMMAND $<TARGET_FILE:audio_es8311_test>
    COMMAND $<TARGET_FILE:event_bus_test>
    COMMAND $<TARGET_FILE:buffer_pool_test>
    COMMAND $<TARGET_FILE:service_camera_test>
    DEPENDS dal_registry_test dal_relay_test bsp_relay_test
            attiny88_test tc358762_test touch_ft5406_test
            audio_codec_test audio_es8311_test event_bus_test
            buffer_pool_test service_camera_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all host unit tests"
)
