/**
 * @file    board_v1.h
 * @brief   Board V1 板级组装器入口与共享资源访问
 *
 * @details board_v1 充当板级组装器：
 *          1. 初始化共享资源（共享 I2C 总线）
 *          2. 按依赖顺序调用各 BSP 的 bsp_xxx_create() 获取 ops+ctx 绑定
 *          3. 调用 dal_xxx_register() 完成设备注册
 *
 *          本组装器**不触发任何硬件初始化**——create() 零副作用，
 *          硬件初始化由上层（Assembler/main）通过 ops->init() 按需触发。
 *          Service 层不应包含本文件；共享总线仅由 BSP 内部使用。
 *          board_i2c_get_bus() 返回 ESP-IDF i2c_master_bus_handle_t
 *          （以 void* 形式，避免本头依赖 driver/i2c_master.h）。
 *
 * @author  xiamu
 * @version 1.1
 */
#ifndef BOARD_V1_H
#define BOARD_V1_H

#include <stdbool.h>
#include "dal_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 板级组装：初始化共享 I2C 总线 + 各 BSP create + DAL register
 *
 * @details 顺序：
 *          1. 共享 I2C 总线（display/camera/touch/audio 共用）
 *          2. SDMMC 存储挂载（板级基础设施，非 DAL 设备）
 *          3-9. 各设备 create() + dal_xxx_register()
 *
 *          任一非关键失败仅日志告警、不中止后续注册，整体返回 DAL_OK。
 *          硬件初始化未在此触发，需上层调 ops->init()。
 *
 * @return DAL_OK 完成（可能含告警）
 */
dal_err_t board_v1_init(void);

/**
 * @brief 获取共享 I2C 总线句柄
 *
 * @return i2c_master_bus_handle_t（以 void* 返回）；NULL 表示总线未初始化
 *
 * @note 仅在 board_v1_init() 之后有效。供 display/camera/touch/audio BSP
 *       内部 i2c_master_bus_add_device 使用。
 */
void *board_i2c_get_bus(void);

#ifdef __cplusplus
}
#endif
#endif /* BOARD_V1_H */
