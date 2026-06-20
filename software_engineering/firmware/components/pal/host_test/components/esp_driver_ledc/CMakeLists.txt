# 覆盖 esp_driver_ledc 组件，mock driver/ledc.h
set(original_dir "$ENV{IDF_PATH}/components/esp_driver_ledc")

idf_component_mock(
    INCLUDE_DIRS
        "${original_dir}/include/driver"
        "${original_dir}/include"
    REQUIRES
        hal
        soc
        driver
    MOCK_HEADER_FILES
        "${original_dir}/include/driver/ledc.h"
)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../soc_caps_defs.cmake")
apply_soc_caps(${COMPONENT_LIB})
