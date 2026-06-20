# 覆盖 esp_driver_i2c 组件，mock driver/i2c_master.h（新版 master API）
set(original_dir "$ENV{IDF_PATH}/components/esp_driver_i2c")

idf_component_mock(
    INCLUDE_DIRS
        "${original_dir}/include/driver"
        "${original_dir}/include"
    REQUIRES
        hal
        soc
    MOCK_HEADER_FILES
        "${original_dir}/include/driver/i2c_master.h"
)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../soc_caps_defs.cmake")
apply_soc_caps(${COMPONENT_LIB})
