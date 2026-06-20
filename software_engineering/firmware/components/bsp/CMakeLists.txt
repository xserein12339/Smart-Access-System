idf_component_register(
    SRCS
        "bsp_storage_sdmmc/bsp_storage_sdmmc.c"
        "bsp_relay/bsp_relay.c"
        "bsp_display_rpi7pin/bsp_display_rpi7pin.c"
        "bsp_display_rpi7pin/bsp_display_tc358762.c"
        "bsp_display_rpi7pin/bsp_display_attiny88.c"
        "bsp_camera_ov5647/bsp_camera_ov5647.c"
        "bsp_touch_ft5406/bsp_touch_ft5406.c"
        "bsp_audio_es8311/bsp_audio_es8311.c"
        "bsp_audio_es8311/bsp_audio_es8311_codec.c"
        "bsp_network_ip101/bsp_network_ip101.c"
        "bsp_pir_hcsr501/bsp_pir_hcsr501.c"
        "board_v1/board_v1.c"
    INCLUDE_DIRS
        "bsp_storage_sdmmc/include"
        "bsp_relay/include"
        "bsp_display_rpi7pin/include"
        "bsp_camera_ov5647/include"
        "bsp_touch_ft5406/include"
        "bsp_audio_es8311/include"
        "bsp_network_ip101/include"
        "bsp_pir_hcsr501/include"
        "board_v1/include"
        "bsp_config/include"
        "bsp_display_rpi7pin"          # 子芯片内部头文件目录
        "bsp_audio_es8311"             # codec 内部头文件目录
    REQUIRES
        pal osal dal
        esp_eth esp_netif)             # network BSP 直接用 esp_eth
