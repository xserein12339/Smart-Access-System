# 覆盖 esp_hw_support 组件，mock esp_ldo_regulator.h
# 用 IDF_PATH 绝对路径定位原头文件（COMPONENT_OVERRIDEN_DIR 时序不可靠）
set(original_dir "$ENV{IDF_PATH}/components/esp_hw_support")

idf_component_mock(
    INCLUDE_DIRS
        "${original_dir}/include"
        "${original_dir}/ldo/include"
    MOCK_HEADER_FILES
        "${original_dir}/ldo/include/esp_ldo_regulator.h"
)
