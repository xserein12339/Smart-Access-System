# 覆盖 esp_system 组件，mock esp_task_wdt.h
# esp_task_wdt.h 依赖 freertos/task.h 的 TaskHandle_t，故 REQUIRES freertos（已用 mock 覆盖）
set(original_dir "$ENV{IDF_PATH}/components/esp_system")

idf_component_mock(
    INCLUDE_DIRS
        "${original_dir}/include"
    REQUIRES
        freertos
    MOCK_HEADER_FILES
        "${original_dir}/include/esp_task_wdt.h"
)
