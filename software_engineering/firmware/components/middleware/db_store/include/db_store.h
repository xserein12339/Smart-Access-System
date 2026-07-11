/**
 * @file    db_store.h
 * @brief   持久化存储抽象 — 人员库/通行记录/系统配置
 *
 * @details 屏蔽底层存储引擎（NVS / SQLite）。本期为 NVS 实现：人员特征与
 *          通行记录以定长 blob 持久化，系统配置以 KV 存储。线程安全（mutex
 *          串行化写）。接口稳定，后续可替换为 SQLite 实现且上层零修改。
 *
 *          设计约束：
 *          - 人员库容量受限（DB_STORE_MAX_PERSONS），适合门禁场景（<1000）。
 *          - 通行记录环形覆盖（DB_STORE_MAX_RECORDS），超出最旧覆盖。
 *          - 特征向量维度对齐 face_engine FACE_FEATURE_DIM。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef DB_STORE_H
#define DB_STORE_H

#include "dal_err.h"
#include "face_engine.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DB_STORE_MAX_PERSONS   256u   /**< 人员库容量 */
#define DB_STORE_MAX_RECORDS   128u   /**< 通行记录容量（环形） */
#define DB_PERSON_NAME_LEN     32u    /**< 人员姓名最大长度（含 '\0'） */

/** 人员记录 */
typedef struct {
    uint32_t       id;                          /**< 人员 ID（>0 有效） */
    char           name[DB_PERSON_NAME_LEN];    /**< 姓名 */
    face_feature_t feature;                     /**< 特征向量 */
    uint8_t        permission;                  /**< 权限位掩码（bit0=允许通行） */
} db_person_t;

/** 通行记录 */
typedef struct {
    uint32_t person_id;      /**< 人员 ID，0=未识别陌生人 */
    uint32_t timestamp;      /**< 时间戳（秒） */
    uint8_t  result;         /**< 0=PASS,1=REJECT,2=ERROR */
    uint8_t  method;         /**< 0=人脸,1=远程开门,2=其它 */
} db_record_t;

/**
 * @brief 初始化存储（从 SD 卡加载到 PSRAM 缓存）
 * @return DAL_OK 成功
 */
dal_err_t db_store_init(void);

/** @brief 反初始化（退出前 flush） */
dal_err_t db_store_deinit(void);

/**
 * @brief 将脏数据持久化到 SD 卡（系统待机/空闲时调用）
 * @note 仅在 s_dirty 为 true 时执行 I/O，无脏数据直接返回
 */
void db_store_flush(void);

/* ---- 人员 CRUD ---- */
dal_err_t db_person_add(const db_person_t *person);
dal_err_t db_person_get(uint32_t id, db_person_t *out);
dal_err_t db_person_del(uint32_t id);
/** @brief 列出全部人员（拷贝到调用方数组），返回实际数量 */
uint16_t  db_person_list(db_person_t *out, uint16_t max_count);
/** @brief 当前人员数量 */
uint16_t  db_person_count(void);

/* ---- 通行记录 ---- */
dal_err_t db_record_add(const db_record_t *record);
/** @brief 查询最近 n 条记录（按时间倒序），返回实际数量 */
uint16_t  db_record_query_recent(db_record_t *out, uint16_t max_count);

/* ---- 系统配置 KV ---- */
dal_err_t db_config_set_u32(const char *key, uint32_t value);
dal_err_t db_config_get_u32(const char *key, uint32_t *out, uint32_t def);
dal_err_t db_config_set_str(const char *key, const char *value);
dal_err_t db_config_get_str(const char *key, char *out, uint16_t len, const char *def);

#ifdef __cplusplus
}
#endif
#endif /* DB_STORE_H */
