/**
 * @file    esp_err.h
 * @brief   Mock esp_err.h — ESP-IDF 错误码（宿主机测试用）
 *
 * 仅定义 PAL 层实际使用的错误码。实际 ESP-IDF 有更复杂的定义，
 * Mock 版本简化到最小。
 */

#ifndef MOCK_ESP_ERR_H
#define MOCK_ESP_ERR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief ESP-IDF 错误类型 */
typedef int32_t esp_err_t;

#define ESP_OK                      0
#define ESP_FAIL                    -1
#define ESP_ERR_NO_MEM              0x101
#define ESP_ERR_INVALID_ARG         0x102
#define ESP_ERR_INVALID_STATE       0x103
#define ESP_ERR_INVALID_SIZE        0x104
#define ESP_ERR_NOT_FOUND           0x105
#define ESP_ERR_NOT_SUPPORTED       0x106
#define ESP_ERR_TIMEOUT             0x107
#define ESP_ERR_INVALID_RESPONSE    0x108
#define ESP_ERR_INVALID_CRC         0x109
#define ESP_ERR_INVALID_VERSION     0x10A
#define ESP_ERR_WIFI_BASE           0x3000

#ifdef __cplusplus
}
#endif

#endif /* MOCK_ESP_ERR_H */
