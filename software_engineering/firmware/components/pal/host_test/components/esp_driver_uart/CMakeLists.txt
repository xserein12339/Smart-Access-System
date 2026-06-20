# 覆盖 esp_driver_uart 组件，mock driver/uart.h
set(original_dir "$ENV{IDF_PATH}/components/esp_driver_uart")

idf_component_mock(
    INCLUDE_DIRS
        "${original_dir}/include/driver"
        "${original_dir}/include"
    REQUIRES
        freertos
        hal
        soc
    MOCK_HEADER_FILES
        "${original_dir}/include/driver/uart.h"
)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../soc_caps_defs.cmake")
apply_soc_caps(${COMPONENT_LIB})
