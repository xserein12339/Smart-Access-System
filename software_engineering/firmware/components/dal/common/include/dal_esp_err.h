/**
 * @file    dal_esp_err.h
 * @brief   esp_err_t 到 dal_err_t 的公共翻译
 *
 * @details 架构规范要求底层错误码在 BSP ops 边界翻译为 dal_err_t，不得透传。
 *          BSP 直接调用 ESP-IDF API，返回 esp_err_t。本文件提供公共翻译函数，
 *          避免每个 BSP 重复实现。
 *
 *          翻译规则：
 *          - ESP_OK            → DAL_OK
 *          - ESP_ERR_INVALID_ARG → DAL_ERR_INVALID
 *          - ESP_ERR_TIMEOUT   → DAL_ERR_TIMEOUT
 *          - ESP_ERR_NOT_FOUND → DAL_ERR_NOT_FOUND
 *          - ESP_ERR_NO_MEM    → DAL_ERR_NO_MEM
 *          - 其余              → DAL_ERR_HW（具体码不向上层泄露）
 *
 * @author  xiamu
 * @version 1.0
 */
#ifndef DAL_ESP_ERR_H
#define DAL_ESP_ERR_H

#include "dal_err.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将 esp_err_t 翻译为 dal_err_t
 *
 * @param[in] err ESP-IDF 函数返回值
 * @return 对应的 dal_err_t
 *
 * @note 本函数不泄露具体 esp_err_t，仅按语义归类。
 */
static inline dal_err_t dal_err_from_esp(esp_err_t err)
{
    if (err == ESP_OK) {
        return DAL_OK;
    }
    switch (err) {
    case ESP_ERR_INVALID_ARG:  return DAL_ERR_INVALID;
    case ESP_ERR_INVALID_STATE:return DAL_ERR_STATE;
    case ESP_ERR_TIMEOUT:      return DAL_ERR_TIMEOUT;
    case ESP_ERR_NOT_FOUND:    return DAL_ERR_NOT_FOUND;
    case ESP_ERR_NO_MEM:       return DAL_ERR_NO_MEM;
    case ESP_ERR_NOT_SUPPORTED:return DAL_ERR_UNSUPPORTED;
    default:                   return DAL_ERR_HW;
    }
}

#ifdef __cplusplus
}
#endif
#endif /* DAL_ESP_ERR_H */
