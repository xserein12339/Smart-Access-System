# 覆盖 esp_driver_i2s 组件，mock driver/i2s_std.h + driver/i2s_common.h
# i2s_new_channel / i2s_del_channel / i2s_channel_* 在 i2s_common.h，
# i2s_channel_init_std_mode 在 i2s_std.h，两者都需 mock 才能覆盖 pal_i2s 全部底层调用
set(original_dir "$ENV{IDF_PATH}/components/esp_driver_i2s")

idf_component_mock(
    INCLUDE_DIRS
        "${original_dir}/include/driver"
        "${original_dir}/include"
    REQUIRES
        hal
        soc
    MOCK_HEADER_FILES
        "${original_dir}/include/driver/i2s_std.h"
        "${original_dir}/include/driver/i2s_common.h"
)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../soc_caps_defs.cmake")
apply_soc_caps(${COMPONENT_LIB})
