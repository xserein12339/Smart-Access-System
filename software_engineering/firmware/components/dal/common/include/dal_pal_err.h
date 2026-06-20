/**
 * @file    dal_pal_err.h
 * @brief   PAL 错误码到 dal_err_t 的公共翻译
 *
 * @details PAL 各模块返回 int（约定 0 成功，负数为 esp_err_t 兼容错误码）。
 *          架构规范要求底层错误码在 BSP ops 边界翻译为 dal_err_t，不得透传。
 *          本文件提供公共翻译函数，避免每个 BSP 重复实现。
 *
 *          翻译规则：
 *          - ESP_OK            → DAL_OK
 *          - ESP_ERR_INVALID_ARG → DAL_ERR_INVALID
 *          - ESP_ERR_TIMEOUT   → DAL_ERR_TIMEOUT
 *          - ESP_ERR_NOT_FOUND → DAL_ERR_NOT_FOUND
 *          - ESP_ERR_NO_MEM    → DAL_ERR_NO_MEM
 *          - 其余              → DAL_ERR_HW（具体码不向上层泄露）
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef DAL_PAL_ERR_H
#define DAL_PAL_ERR_H

#include "dal_err.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将 PAL 返回码（esp_err_t 兼容）翻译为 dal_err_t
 *
 * @param[in] pal_ret PAL 函数返回值（0 成功，负数 esp_err_t 错误码）
 * @return 对应的 dal_err_t
 *
 * @note 本函数不泄露具体 esp_err_t，仅按语义归类。
 */
static inline dal_err_t dal_err_from_pal(int pal_ret)
{
    if (pal_ret == ESP_OK) {
        return DAL_OK;
    }
    switch (pal_ret) {
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
#endif /* DAL_PAL_ERR_H */
