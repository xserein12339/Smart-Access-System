/**
 * @file    board_v1.h
 * @brief   Mock board_v1.h — 板级共享资源访问（宿主机测试用）
 *
 * @details board_i2c_get_bus 返回非 NULL 占位，模拟共享 I2C 总线已就绪。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef MOCK_BOARD_V1_H
#define MOCK_BOARD_V1_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline bool board_v1_init(void)
{
    return true;
}

/** @brief 返回共享 I2C 总线句柄（mock：非 NULL 占位） */
static inline void *board_i2c_get_bus(void)
{
    return (void *)1;
}

#ifdef __cplusplus
}
#endif
#endif /* MOCK_BOARD_V1_H */
