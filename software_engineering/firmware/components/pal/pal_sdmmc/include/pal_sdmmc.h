/**
 * @file    pal_sdmmc.h
 * @brief   PAL SDMMC 模块 — SD 卡 FAT 文件系统挂载
 *
 * 封装 ESP-IDF esp_vfs_fat_sdmmc_mount，初始化 SDMMC 主机 + 挂载 FATFS 到
 * 指定 VFS 路径，挂载后可用标准 fopen/fread/fwrite 读写文件。
 * 适用于存放人脸库、日志、模型等持久化数据。
 *
 * 引脚留空（-1）时使用 ESP32-P4 默认引脚：
 *   clk=43, cmd=44, d0=39, d1=40, d2=41, d3=42
 *
 * 参考文档：ESP32-P4 TRM SDMMC 章节
 * @author  Access System Firmware Team
 * @version 1.0
 */

#ifndef PAL_SDMMC_H
#define PAL_SDMMC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief SDMMC 句柄（内部持有 sdmmc_card_t*） */
typedef void *pal_sdmmc_handle_t;

/* ================================================================
 *  配置结构体
 * ================================================================ */

/**
 * @brief SDMMC 挂载配置
 */
typedef struct {
    int         clk_pin;    /**< CLK 引脚，-1 使用默认(43) */
    int         cmd_pin;    /**< CMD 引脚，-1 使用默认(44) */
    int         d0_pin;     /**< D0 引脚，-1 使用默认(39) */
    int         d1_pin;     /**< D1 引脚，-1 使用默认(40) */
    int         d2_pin;     /**< D2 引脚，-1 使用默认(41) */
    int         d3_pin;     /**< D3 引脚，-1 使用默认(42) */
    int         bus_width;  /**< 总线宽度 1/4/8，0=使用最大可用 */
    uint32_t    freq_hz;    /**< 卡时钟频率（Hz），0=使用默认(20MHz) */
    const char *mount_point;/**< VFS 挂载点，如 "/sdcard" */
    bool        format_if_mount_failed; /**< 挂载失败时是否格式化 */
    int         max_files;  /**< 同时打开文件数上限，0=默认 5 */
} pal_sdmmc_config_t;

/* ================================================================
 *  生命周期 API
 * ================================================================ */

/**
 * @brief 初始化 SDMMC 主机并挂载 FAT 文件系统
 *
 * @param[out] handle 返回的句柄
 * @param[in]  cfg    挂载配置
 * @return 0 成功，负数失败
 *
 * @note 挂载成功后即可用 fopen(cfg->mount_point/...) 访问文件系统。
 */
int pal_sdmmc_mount(pal_sdmmc_handle_t *handle, const pal_sdmmc_config_t *cfg);

/**
 * @brief 卸载 FAT 文件系统并释放 SDMMC 资源
 *
 * @param handle 句柄
 * @return 0 成功，负数失败
 */
int pal_sdmmc_unmount(pal_sdmmc_handle_t handle);

/* ================================================================
 *  信息 API
 * ================================================================ */

/**
 * @brief 获取底层 sdmmc_card_t 指针（用于 sdmmc_card_print_info 等查询）
 *
 * @param handle 句柄
 * @return sdmmc_card_t* 指针，无效句柄返回 NULL
 */
void *pal_sdmmc_get_card(pal_sdmmc_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* PAL_SDMMC_H */
