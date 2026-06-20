/**
 * @file    bsp_storage_sdmmc.h
 * @brief   V1 板卡 SDMMC 存储初始化入口（文件语义）
 *
 * @details 仅初始化 SDMMC 并挂载 FAT 文件系统，不封装 DAL。
 *          挂载成功后业务层直接用标准 stdio（fopen/fread/...）操作
 *          /sdcard 下的文件。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef BSP_STORAGE_SDMMC_H
#define BSP_STORAGE_SDMMC_H

#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 SDMMC 并挂载 FAT 文件系统
 *
 * @return DAL_OK 成功
 * @retval DAL_ERR_HW 挂载失败（硬件错误）
 *
 * @note 挂载成功后，应用层可直接使用 fopen("/sdcard/xxx", ...)。
 *       仅可调用一次，重复调用直接返回 DAL_OK（幂等）。
 */
dal_err_t bsp_storage_sdmmc_init(void);

#ifdef __cplusplus
}
#endif
#endif /* BSP_STORAGE_SDMMC_H */
