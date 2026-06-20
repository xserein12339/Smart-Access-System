/**
 * @file    board_v1.h
 * @brief   Board V1 板级初始化入口与共享资源访问
 *
 * @details board_v1 统一管理板级共享资源（如共享 I2C 总线），并按序
 *          初始化各 BSP 子系统（触发其自注册到 DAL）。
 *          - Service 层不应包含本文件；共享总线仅由 BSP 内部使用。
 *          - board_i2c_get_bus() 返回 PAL I2C 总线句柄（void* 形式，
 *            调用方按需转回 pal_i2c_bus_handle_t），避免本头依赖 PAL。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef BOARD_V1_H
#define BOARD_V1_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 板级初始化：初始化共享 I2C 总线，并按序调用各 BSP init
 *
 * @details 初始化顺序（严格，违反会导致依赖未就绪）：
 *          1. 共享 I2C 总线（供 display/camera/touch 共用）
 *          2. SDMMC 存储
 *          3. 显示屏（依赖 I2C 初始化 ATTINY88）
 *          4. 摄像头（依赖 I2C 的 SCCB）
 *          5. 触摸（依赖 I2C）
 *          6. 继电器
 *
 * @return true 全部成功；false 任一失败（已初始化部分保留，日志告警）
 */
bool board_v1_init(void);

/**
 * @brief 获取共享 I2C 总线句柄
 *
 * @return PAL I2C 总线句柄（void*）；NULL 表示总线未初始化
 *
 * @note 仅在 board_v1_init() 之后有效。供 display/camera/touch BSP 内部使用。
 */
void *board_i2c_get_bus(void);

#ifdef __cplusplus
}
#endif
#endif /* BOARD_V1_H */
