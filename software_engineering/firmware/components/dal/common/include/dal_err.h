/**
 * @file    dal_err.h
 * @brief   DAL 公共错误码定义
 *
 * @details 本文件定义 DAL/SVC 层统一错误码 dal_err_t。所有 DAL 接口契约、
 *          DAL 管理 API、Service 依赖契约的返回值必须使用本类型。
 *          底层 HAL/SDK 错误码（如 esp_err_t、PAL 返回码）必须在 BSP 的
 *          ops 边界翻译为 dal_err_t，原始码不得透传到 DAL/SVC。
 *
 * @author  Access System Firmware Team
 * @version 1.0
 */
#ifndef DAL_ERR_H
#define DAL_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DAL 统一错误码
 *
 * @note 取值遵循「0 成功，负数失败」惯例，便于上层用 `if (ret == DAL_OK)`
 *       或 `if (ret < 0)` 判断。
 */
typedef enum {
    DAL_OK              =  0,  /**< 成功 */
    DAL_ERR_INVALID     = -1,  /**< 参数非法 */
    DAL_ERR_NO_MEM      = -2,  /**< 无内存 / 注册表满 */
    DAL_ERR_NOT_FOUND   = -3,  /**< 设备未注册 */
    DAL_ERR_BUSY        = -4,  /**< 设备忙 */
    DAL_ERR_TIMEOUT     = -5,  /**< 超时 */
    DAL_ERR_HW          = -6,  /**< 底层硬件错误（已翻译，不泄露原始码） */
    DAL_ERR_STATE       = -7,  /**< 状态非法（如重复注册、名称冲突） */
    DAL_ERR_UNSUPPORTED = -8,  /**< 操作不支持 / 版本不兼容 */
} dal_err_t;

#ifdef __cplusplus
}
#endif
#endif /* DAL_ERR_H */
