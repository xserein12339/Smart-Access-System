/**
 * @file    board_v1_config.h
 * @brief   ESP32-P4-WIFI6-DEV-KIT 板级配置文件
 *
 * @details 本文件集中定义开发板所有外设的硬件引脚映射与运行参数。
 *          各 BSP 模块通过 #include 本文件获取引脚号、I2C 地址、时序参数等，
 *          避免在驱动代码中硬编码板级细节。
 *
 *          硬件平台：ESP32-P4-WIFI6-DEV-KIT
 *          主控芯片：ESP32-P4NRW32
 *          WiFi 芯片：ESP32-C6FH8（SDIO 连接）
 *
 *          参考文档：
 *          - hardware_engineering/ESP32-P4-WIFI6-DEV-KIT-datasheet.pdf（原理图）
 *          - ESP32-P4 Technical Reference Manual
 *
 * @author  xiamu
 * @version 2.0
 */

#ifndef BOARD_V1_CONFIG_H
#define BOARD_V1_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  1. 共享 I2C 总线（触控 / 背光 / 音频 Codec 共用）
 *
 *  SDA=GPIO7, SCL=GPIO8，400kHz 标准模式 + 内部上拉。
 *  总线设备：FT5406(0x38)、ATTINY88(0x45)、ES8311(0x18)、OV5647 SCCB。
 * ================================================================ */
#define BOARD_I2C_PORT                  0       /**< I2C 端口号 */
#define BOARD_I2C_SDA_PIN               7       /**< SDA 引脚 */
#define BOARD_I2C_SCL_PIN               8       /**< SCL 引脚 */
#define BOARD_I2C_FREQ_HZ               400000  /**< I2C 时钟频率 (Hz) */
#define BOARD_I2C_ENABLE_PULLUP         true    /**< 使能内部上拉电阻 */
#define BOARD_I2C_INTR_PRIORITY         0       /**< 中断优先级（0=驱动默认） */
#define BOARD_I2C_TRANS_QUEUE_DEPTH     0       /**< 传输队列深度（0=驱动默认） */

/* ================================================================
 *  2. 人体红外感应（PIR）：HC-SR501
 *
 *  HC-SR501 输出高电平表示检测到人体运动。GPIO 配置为输入 + 下拉，
 *  使用双沿中断（GPIO_INTR_ANYEDGE）驱动回调，去抖/离开确认在 service
 *  任务上下文完成。
 * ================================================================ */
#define BOARD_PIR_INTR_PIN              1       /**< PIR 信号输入引脚 */

/* ================================================================
 *  3. 继电器输出
 *
 *  本板 3 路继电器，各由独立 GPIO 控制（高电平吸合）。
 *  实例名：door_lock / alarm / wiegand_pwr
 *
 *  @note bsp_relay.c 通过 bsp_relay_create("实例名") 选择对应引脚。
 *        硬件上 door_lock 与 alarm 为独立继电器，不可共用 GPIO。
 * ================================================================ */
#define BOARD_RELAY_DOOR_PIN            2       /**< 门锁继电器 */
#define BOARD_RELAY_ALARM_PIN           4       /**< 报警继电器 */
#define BOARD_RELAY_WIEGAND_PIN         3       /**< 韦根电源继电器 */

/** @brief 继电器有效电平：true=高电平吸合，false=低电平吸合 */
#define BOARD_RELAY_ACTIVE_HIGH         true

/* ================================================================
 *  4. MicroSD 卡（SDMMC 4-bit 模式）
 *
 *  CD (Card Detect) 引脚 GPIO45 同时控制 SD 卡电源 MOSFET (AO3401)，
 *  低电平时断开 SD 卡供电，可用于 SD 卡异常时的硬件复位。
 * ================================================================ */
#define BOARD_STORAGE_D0_PIN            39      /**< SDMMC Data0 */
#define BOARD_STORAGE_D1_PIN            40      /**< SDMMC Data1 */
#define BOARD_STORAGE_D2_PIN            41      /**< SDMMC Data2 */
#define BOARD_STORAGE_D3_PIN            42      /**< SDMMC Data3 */
#define BOARD_STORAGE_CMD_PIN           44      /**< SDMMC Command */
#define BOARD_STORAGE_CLK_PIN           43      /**< SDMMC Clock */
#define BOARD_STORAGE_CD_PIN            45      /**< SD 卡检测 + 电源控制 */
#define BOARD_STORAGE_WP_PIN            -1      /**< 写保护检测（本板未连接） */
#define BOARD_STORAGE_FORMAT_IF_MOUNT_FAILED  false   /**< 挂载失败时不自动格式化 */
#define BOARD_STORAGE_MAX_FILES         5       /**< 同时打开最大文件数 */
#define BOARD_STORAGE_FREQ_KHZ          20000   /**< SDMMC 时钟频率 (kHz) */
#define BOARD_STORAGE_USE_1BIT          false   /**< false=4-bit 模式，true=1-bit */
#define BOARD_STORAGE_MOUNT_POINT       "/sdcard"   /**< 挂载点路径 */

/* ================================================================
 *  5. 音频模块：ES8311 Codec + NS4150B 功放
 *
 *  I2S 全双工主机模式，16-bit 立体声 Philips 格式。
 *  ES8311 寄存器配置经共享 I2C 总线（地址 0x18）。
 *  PA_CTRL (GPIO11) 与耳机检测经 SN74LVC1G08 AND 门后控制 NS4150B 功放使能。
 *  Codec_CE (GPIO53) 为 ES8311 芯片使能（高有效），板上 10K 上拉。
 * ================================================================ */
#define BOARD_AUDIO_CODEC_I2C_ADDR       0x18    /**< ES8311 I2C 地址（7-bit） */
#define BOARD_AUDIO_CODEC_CE_PIN         53      /**< ES8311 芯片使能（CE，高有效） */
#define BOARD_AUDIO_I2S_PORT             0       /**< I2S 端口号 */
#define BOARD_AUDIO_MCLK_PIN             13      /**< I2S 主时钟 (MCLK) */
#define BOARD_AUDIO_BCLK_PIN             12      /**< I2S 位时钟 (BCLK/SCLK) */
#define BOARD_AUDIO_LRCK_PIN             10      /**< I2S 左右声道时钟 (LRCK/WS) */
#define BOARD_AUDIO_DOUT_PIN             9       /**< I2S 数据输出 (ASDOUT → DAC) */
#define BOARD_AUDIO_DIN_PIN              48      /**< I2S 数据输入 (DSDIN ← ADC) */
#define BOARD_AUDIO_PA_EN_PIN            11      /**< 功放使能（PA_CTRL，高有效） */
#define BOARD_AUDIO_SAMPLE_RATE          16000   /**< 默认采样率 (Hz) */

/* 自检音源参数 */
#define BOARD_AUDIO_TEST_TONE_FREQ_HZ    1000    /**< 测试音频率 (Hz) */
#define BOARD_AUDIO_TEST_TONE_DURATION_MS 200    /**< 测试音持续时长 (ms) */
#define BOARD_AUDIO_TEST_TONE_AMPLITUDE  3000    /**< 测试音幅度（16-bit PCM） */

/* ================================================================
 *  6. 触摸模块：Raspberry Pi 7" Touch Display / FT5406
 *
 *  电容触摸控制器 FT5406，经共享 I2C 总线通信，地址 0x38。
 *  触摸坐标分辨率与显示屏一致（800×480）。
 * ================================================================ */
#define BOARD_TOUCH_FT5406_I2C_ADDR      0x38    /**< FT5406 I2C 地址（7-bit） */
#define BOARD_TOUCH_H_RES                800     /**< 触摸水平分辨率 */
#define BOARD_TOUCH_V_RES                480     /**< 触摸垂直分辨率 */

/* ================================================================
 *  7. 显示模块：Raspberry Pi 7" Touch Display
 *
 *  方案：DSI → TC358762 RGB 桥接芯片 + ATTINY88 背光控制。
 *  ATTINY88 经共享 I2C 总线通信（I2C 地址 0x45），负责上电时序、
 *  复位释放、PWM 背光调节。
 *
 *  DSI 接口：1-lane，600 Mbps/lane，DPI 时钟 25.98 MHz。
 *  时序参数适配 800×480 @60Hz。
 * ================================================================ */
#define BOARD_DISPLAY_USE_ATTINY88               true    /**< 使用 ATTINY88 背光控制 */
#define BOARD_DISPLAY_ATTINY88_I2C_ADDR          0x45    /**< ATTINY88 I2C 地址（7-bit） */
#define BOARD_DISPLAY_DEFAULT_BRIGHTNESS         50      /**< 默认亮度百分比 (0~100) */
#define BOARD_DISPLAY_POWER_TO_BACKLIGHT_MS      80      /**< 上电到背光使能延迟 (ms) */

/* DSI 配置 */
#define BOARD_DISPLAY_DSI_HOST                   0       /**< DSI 主机编号 */
#define BOARD_DISPLAY_DSI_NUM_DATA_LANES         1       /**< DSI 数据通道数 */
#define BOARD_DISPLAY_DSI_LANE_BIT_RATE_MBPS     600     /**< DSI 每通道速率 (Mbps) */
#define BOARD_DISPLAY_DSI_VIRTUAL_CHANNEL        0       /**< DSI 虚拟通道 */

/* 显示分辨率 */
#define BOARD_DISPLAY_H_RES                      800     /**< 水平分辨率 */
#define BOARD_DISPLAY_V_RES                      480     /**< 垂直分辨率 */
#define BOARD_DISPLAY_PIXEL_FORMAT               2       /**< 像素格式（2=RGB565） */
#define BOARD_DISPLAY_IN_COLOR_FORMAT            1       /**< 输入颜色格式 */
#define BOARD_DISPLAY_NUM_FBS                    2       /**< FrameBuffer 数量（2=双缓冲，DPI panel vsync 交换免撕裂） */

/* DPI 时序（适配 TC358762 输入要求） */
#define BOARD_DISPLAY_DPI_CLOCK_FREQ_MHZ         25.98f  /**< DPI 像素时钟 (MHz) */
#define BOARD_DISPLAY_HSYNC_PULSE_WIDTH          2       /**< 水平同步脉宽 (pixel clock) */
#define BOARD_DISPLAY_HSYNC_BACK_PORCH           46      /**< 水平后肩 */
#define BOARD_DISPLAY_HSYNC_FRONT_PORCH          210     /**< 水平前肩 */
#define BOARD_DISPLAY_VSYNC_PULSE_WIDTH          20      /**< 垂直同步脉宽 (line) */
#define BOARD_DISPLAY_VSYNC_BACK_PORCH           4       /**< 垂直后肩 */
#define BOARD_DISPLAY_VSYNC_FRONT_PORCH          22      /**< 垂直前肩 */
#define BOARD_DISPLAY_HSYNC_POLARITY             0       /**< 水平同步极性（0=低有效） */
#define BOARD_DISPLAY_VSYNC_POLARITY             0       /**< 垂直同步极性（0=低有效） */

/* 背光 PWM（经 ATTINY88 I2C 控制，非直连 GPIO） */
#define BOARD_DISPLAY_BL_USE_DIRECT_PWM          false   /**< false=ATTINY88 I2C，true=GPIO PWM */
#define BOARD_DISPLAY_BL_GPIO                    -1      /**< 直连 PWM 时的 GPIO（未使用） */
#define BOARD_DISPLAY_BL_LEDC_CHANNEL            0       /**< LEDC 通道（直连模式） */
#define BOARD_DISPLAY_BL_LEDC_TIMER              0       /**< LEDC 定时器（直连模式） */
#define BOARD_DISPLAY_BL_FREQ_HZ                 5000    /**< 背光 PWM 频率 (Hz) */

/* ================================================================
 *  8. 以太网模块：IP101 PHY（RMII 接口）
 *
 *  ESP32-P4 内部 EMAC 通过 RMII 连接 IP101 PHY，板载 RJ45 带 POE
 *  （HBJ-6117ANL）。PHY 地址由 PHY_AD0/PHY_AD3 引脚电平决定。
 * ================================================================ */
#define BOARD_NETWORK_ETH_MDC_PIN            31      /**< EMAC MDC 引脚 */
#define BOARD_NETWORK_ETH_MDIO_PIN           52      /**< EMAC MDIO 引脚 */
#define BOARD_NETWORK_ETH_PHY_RESET_PIN      51      /**< PHY 复位引脚（低有效） */
#define BOARD_NETWORK_ETH_PHY_ADDR           1       /**< PHY 地址（PHY_AD[3:0] 电平） */
#define BOARD_NETWORK_ETH_PHY_TYPE           3       /**< PHY 类型（ESP-IDF 内部枚举） */

/* 网络参数 */
#define BOARD_NETWORK_USE_DHCP               true    /**< true=DHCP，false=静态 IP */
#define BOARD_NETWORK_STATIC_IP              ""      /**< 静态 IP（仅 DHCP=false 时有效） */
#define BOARD_NETWORK_NETMASK                ""      /**< 子网掩码 */
#define BOARD_NETWORK_GATEWAY                ""      /**< 网关 */

/* RMII 数据引脚（由 ESP-IDF EMAC 驱动内部管理，此处仅供文档参考）
 *
 *   TXD0=GPIO29, TXD1=GPIO30, TXEN=GPIO28, TXCLK=GPIO50
 *   RXD0=GPIO34, RXD1=GPIO35, CRS_DV=GPIO33, RXCLK=GPIO49
 */
#define BOARD_NETWORK_RMII_TXD0_PIN          29      /**< RMII TXD0 */
#define BOARD_NETWORK_RMII_TXD1_PIN          30      /**< RMII TXD1 */
#define BOARD_NETWORK_RMII_TXEN_PIN          28      /**< RMII TX_EN */
#define BOARD_NETWORK_RMII_RXD0_PIN          34      /**< RMII RXD0 */
#define BOARD_NETWORK_RMII_RXD1_PIN          35      /**< RMII RXD1 */
#define BOARD_NETWORK_RMII_CRS_DV_PIN        33      /**< RMII CRS_DV */

/* ================================================================
 *  9. 摄像头模块：OV5647（MIPI CSI-2）
 *
 *  OV5647 通过 MIPI CSI-2（2-lane）连接 ESP32-P4，SCCB 控制信号
 *  复用共享 I2C 总线。使用 V4L2 驱动框架采集。
 *
 *  Sensor 输出 800×640，ISP 裁剪或软件裁剪后输出 800×480。
 *  裁剪配置：top=(640-480)/2=80，输出 799×480（width-1 绕过 V4L2 校验）。
 * ================================================================ */
#define BOARD_CAMERA_SCCB_I2C_PORT           BOARD_I2C_PORT      /**< SCCB I2C 端口 */
#define BOARD_CAMERA_SCCB_SDA_PIN            BOARD_I2C_SDA_PIN   /**< SCCB SDA 引脚 */
#define BOARD_CAMERA_SCCB_SCL_PIN            BOARD_I2C_SCL_PIN   /**< SCCB SCL 引脚 */
#define BOARD_CAMERA_SCCB_FREQ_HZ            100000              /**< SCCB 时钟频率 (Hz) */
#define BOARD_CAMERA_RESET_PIN               -1                  /**< 摄像头复位引脚（-1=未连接） */
#define BOARD_CAMERA_PWDN_PIN                -1                  /**< 摄像头掉电引脚（-1=未连接） */
#define BOARD_CAMERA_DEVICE_PATH             "/dev/video0"       /**< V4L2 设备路径 */
#define BOARD_CAMERA_FRAME_WIDTH             800                 /**< Sensor 输出宽度 */
#define BOARD_CAMERA_FRAME_HEIGHT            640                 /**< Sensor 输出高度 */
#define BOARD_CAMERA_BUFFER_COUNT            3       /**< V4L2 DMA 缓冲数，3 提升流水线深度 */

/* ISP 裁剪（当前使用软件裁剪，因为 ISP crop 与 USERPTR 不兼容） */
#define BOARD_CAMERA_CROP_ENABLE             0       /**< 使能 ISP 硬件裁剪（0=软件裁剪） */
#define BOARD_CAMERA_CROP_WIDTH              799     /**< 裁剪宽度（right<width 校验，800==800 被判非法，-1 绕过） */
#define BOARD_CAMERA_CROP_HEIGHT             480     /**< 裁剪高度 */
#define BOARD_CAMERA_CROP_TOP                80      /**< 裁剪起始行 (top=(640-480)/2) */
#define BOARD_CAMERA_CROP_LEFT               0       /**< 裁剪起始列 */

#define BOARD_CAMERA_DQBUF_TIMEOUT_MS        2000    /**< V4L2 DQBUF 超时 (ms) */
#define BOARD_CAMERA_SELF_TEST_MAX_TRIES     3       /**< 自检最大重试次数 */

/* 预览参数 */
#define BOARD_CAMERA_PREVIEW_ENABLE          1       /**< 使能摄像头预览 */
#define BOARD_CAMERA_PREVIEW_INTERVAL_MS     100     /**< 预览帧间隔 (ms) */

/* ================================================================
 *  10. ESP32-C6 WiFi 协处理器（SDIO 连接）
 *
 *  ESP32-C6FH8 通过 SDIO 2.0 与 ESP32-P4 通信，UART 作为调试/AT 命令通道。
 *  以下引脚由 ESP-IDF esp_hosted 或 AT 固件内部管理，仅供文档参考。
 *
 *  SDIO 信号：
 *    SDIO_CMD  = GPIO19     SDIO_CLK   = GPIO18
 *    SDIO_DAT0 = GPIO14     SDIO_DAT1  = GPIO15
 *    SDIO_DAT2 = GPIO16     SDIO_DAT3  = GPIO17
 *
 *  UART 调试通道：
 *    C6_U0TXD  → GPIO37 (ESP32-P4 RX)
 *    C6_U0RXD  ← GPIO38 (ESP32-P4 TX)
 *
 *  控制信号：
 *    C6_CHIP_PU → GPIO54 (C6 使能)
 *    C6_IO2    → GPIO6  (C6 GPIO2)
 *    C6_IO8    → GPIO26 (C6 GPIO8)
 *    C6_IO9    → GPIO27 (C6 GPIO9)
 * ================================================================ */
#define BOARD_WIFI_C6_CHIP_PU_PIN           54      /**< ESP32-C6 使能引脚 */
#define BOARD_WIFI_C6_IO2_PIN               6       /**< ESP32-C6 GPIO2 */
#define BOARD_WIFI_C6_IO8_PIN               26      /**< ESP32-C6 GPIO8 */
#define BOARD_WIFI_C6_IO9_PIN               27      /**< ESP32-C6 GPIO9 */

/* ================================================================
 *  11. USB-UART 调试串口（CH343P）
 *
 *  USB Type-C 转 UART，支持自动下载（DTR/RTS 控制 EN 与 GPIO0）。
 *  GPIO37(RTS) 和 GPIO38(DTR) 经三极管电路控制芯片复位与下载模式。
 * ================================================================ */
#define BOARD_UART_DTR_PIN                  38      /**< DTR → 自动下载电路 */
#define BOARD_UART_RTS_PIN                  37      /**< RTS → 自动下载电路 */

/* ================================================================
 *  12. 板载按键
 *
 *  KEY1 (BOOT)：GPIO0，低电平进入下载模式
 *  KEY2 (RST) ：CHIP_PU/EN，低电平复位
 * ================================================================ */
#define BOARD_BOOT_KEY_PIN                  0       /**< BOOT 按键（GPIO0，低有效） */

#ifdef __cplusplus
}
#endif

#endif /* BOARD_V1_CONFIG_H */
