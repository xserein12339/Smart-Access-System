/**
 * @file    db_store.c
 * @brief   SD 卡持久化存储 — PSRAM 缓存 + 空闲时写入
 *
 * @details 所有运行时操作仅访问 PSRAM 缓存（O(1)），写操作标记 dirty。
 *          系统待机时调用 db_store_flush() 将脏数据写入 SD 卡 FATFS 文件。
 *          启动时从 SD 卡加载回 PSRAM。
 *
 *          文件布局 (/sdcard/db/)：
 *            persons.dat  — 人员库（二进制顺序存储，记录数在文件头）
 *            records.dat  — 通行记录（二进制顺序存储）
 *            config.dat   — 系统配置 KV（每行 "key value\n"）
 *
 *          线程安全：所有操作经 s_mutex 串行化。
 *
 * @author  xiamu
 * @version 2.0 — SD 卡版
 */
#include "db_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

#define DB_TAG          "DB_STORE"
#define DB_DIR          "/sdcard/db"
#define DB_FILE_PERSONS DB_DIR "/persons.dat"
#define DB_FILE_RECORDS DB_DIR "/records.dat"
#define DB_FILE_CONFIG  DB_DIR "/config.dat"

/* 文件头 —— 方便恢复时校验 */
typedef struct {
    uint32_t magic;          /* 0x46444344 "FDCD" */
    uint16_t count;
    uint16_t max_count;
} db_file_header_t;

#define DB_PERSONS_MAGIC  0x46444344u
#define DB_RECORDS_MAGIC  0x46434442u

/* ================================================================
 *  静态变量
 * ================================================================ */
static db_person_t    *s_persons = NULL;
static uint16_t        s_person_count = 0;
static db_record_t     s_records[DB_STORE_MAX_RECORDS];
static uint16_t        s_record_count = 0;
static SemaphoreHandle_t s_mutex = NULL;
static bool             s_inited  = false;
static bool             s_dirty   = false;   /**< 缓存有未持久化的变更 */

/* 系统配置缓存 */
#define DB_CFG_MAX_KEYS  16
typedef struct {
    char key[32];
    char val[64];
} db_cfg_entry_t;
static db_cfg_entry_t s_cfg[DB_CFG_MAX_KEYS];
static uint8_t        s_cfg_count = 0;

/* ================================================================
 *  内部 — 文件 I/O
 * ================================================================ */

static void ensure_db_dir(void)
{
    struct stat st;
    if (stat(DB_DIR, &st) != 0) {
        mkdir(DB_DIR, 0755);
    }
}

/* ---- 人员库加载/保存 ---- */
static void load_persons(void)
{
    FILE *f = fopen(DB_FILE_PERSONS, "rb");
    if (f == NULL) {
        ESP_LOGI(DB_TAG, "no persons.dat, starting fresh");
        s_person_count = 0;
        return;
    }
    db_file_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != DB_PERSONS_MAGIC) {
        ESP_LOGW(DB_TAG, "persons.dat corrupt header");
        fclose(f);
        s_person_count = 0;
        return;
    }
    uint16_t n = hdr.count;
    if (n > DB_STORE_MAX_PERSONS) n = DB_STORE_MAX_PERSONS;
    size_t rd = fread(s_persons, sizeof(db_person_t), n, f);
    fclose(f);
    s_person_count = (uint16_t)rd;
    ESP_LOGI(DB_TAG, "loaded %u persons from SD", (unsigned)s_person_count);
}

static void save_persons(void)
{
    ensure_db_dir();
    FILE *f = fopen(DB_FILE_PERSONS, "wb");
    if (f == NULL) {
        ESP_LOGE(DB_TAG, "persons.dat write open failed: %d", errno);
        return;
    }
    db_file_header_t hdr = {
        .magic = DB_PERSONS_MAGIC,
        .count = s_person_count,
        .max_count = DB_STORE_MAX_PERSONS,
    };
    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(s_persons, sizeof(db_person_t), s_person_count, f);
    fclose(f);
}

/* ---- 通行记录加载/保存 ---- */
static void load_records(void)
{
    FILE *f = fopen(DB_FILE_RECORDS, "rb");
    if (f == NULL) {
        s_record_count = 0;
        return;
    }
    db_file_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != DB_RECORDS_MAGIC) {
        fclose(f);
        s_record_count = 0;
        return;
    }
    uint16_t n = hdr.count;
    if (n > DB_STORE_MAX_RECORDS) n = DB_STORE_MAX_RECORDS;
    size_t rd = fread(s_records, sizeof(db_record_t), n, f);
    fclose(f);
    s_record_count = (uint16_t)rd;
    ESP_LOGI(DB_TAG, "loaded %u records from SD", (unsigned)s_record_count);
}

static void save_records(void)
{
    ensure_db_dir();
    FILE *f = fopen(DB_FILE_RECORDS, "wb");
    if (f == NULL) return;
    db_file_header_t hdr = {
        .magic = DB_RECORDS_MAGIC,
        .count = (s_record_count < DB_STORE_MAX_RECORDS)
                 ? s_record_count : DB_STORE_MAX_RECORDS,
        .max_count = DB_STORE_MAX_RECORDS,
    };
    uint16_t n = hdr.count;
    /* 倒序写入——最新在前 */
    db_record_t buf[DB_STORE_MAX_RECORDS];
    for (uint16_t i = 0; i < n; i++) {
        uint16_t src = (s_record_count - 1 - i) % DB_STORE_MAX_RECORDS;
        buf[i] = s_records[src];
    }
    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(buf, sizeof(db_record_t), n, f);
    fclose(f);
}

/* ---- 配置加载/保存 ---- */
static void load_config(void)
{
    FILE *f = fopen(DB_FILE_CONFIG, "r");
    if (f == NULL) return;
    s_cfg_count = 0;
    char line[100];
    while (fgets(line, sizeof(line), f) && s_cfg_count < DB_CFG_MAX_KEYS) {
        char key[32], val[64];
        if (sscanf(line, "%31s %63[^\n]", key, val) == 2) {
            snprintf(s_cfg[s_cfg_count].key, sizeof(s_cfg[s_cfg_count].key), "%s", key);
            snprintf(s_cfg[s_cfg_count].val, sizeof(s_cfg[s_cfg_count].val), "%s", val);
            s_cfg_count++;
        }
    }
    fclose(f);
}

static void save_config(void)
{
    ensure_db_dir();
    FILE *f = fopen(DB_FILE_CONFIG, "w");
    if (f == NULL) return;
    for (uint8_t i = 0; i < s_cfg_count; i++) {
        fprintf(f, "%s %s\n", s_cfg[i].key, s_cfg[i].val);
    }
    fclose(f);
}

/* ---- 配置查找 ---- */
static int cfg_find(const char *key)
{
    for (uint8_t i = 0; i < s_cfg_count; i++) {
        if (strcmp(s_cfg[i].key, key) == 0) return (int)i;
    }
    return -1;
}

/* ================================================================
 *  公共 API
 * ================================================================ */

dal_err_t db_store_init(void)
{
    if (s_inited) return DAL_ERR_STATE;

    s_persons = heap_caps_malloc(sizeof(db_person_t) * DB_STORE_MAX_PERSONS,
                                 MALLOC_CAP_SPIRAM);
    if (s_persons == NULL) {
        ESP_LOGE(DB_TAG, "persons psram alloc failed");
        return DAL_ERR_NO_MEM;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        heap_caps_free(s_persons);
        s_persons = NULL;
        return DAL_ERR_NO_MEM;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    load_persons();
    load_records();
    load_config();
    s_dirty = false;
    xSemaphoreGive(s_mutex);

    s_inited = true;
    ESP_LOGI(DB_TAG, "ready: persons=%u records=%u cfg=%u",
             (unsigned)s_person_count, (unsigned)s_record_count, (unsigned)s_cfg_count);
    return DAL_OK;
}

dal_err_t db_store_deinit(void)
{
    if (!s_inited) return DAL_ERR_STATE;
    db_store_flush();  /* 退出前最后持久化 */
    vSemaphoreDelete(s_mutex);
    if (s_persons) { heap_caps_free(s_persons); s_persons = NULL; }
    s_mutex = NULL;
    s_inited = false;
    return DAL_OK;
}

/**
 * @brief 将脏数据持久化到 SD 卡（系统待机时调用）
 */
void db_store_flush(void)
{
    if (!s_inited || !s_dirty) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    ESP_LOGI(DB_TAG, "flushing to SD...");
    save_persons();
    save_records();
    save_config();
    s_dirty = false;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(DB_TAG, "flush done");
}

/* ---- 人员 CRUD ---- */

dal_err_t db_person_add(const db_person_t *person)
{
    if (!s_inited || person == NULL || person->id == 0) return DAL_ERR_INVALID;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool updated = false;
    for (uint16_t i = 0; i < s_person_count; i++) {
        if (s_persons[i].id == person->id) {
            s_persons[i] = *person;
            updated = true;
            break;
        }
    }
    if (!updated) {
        if (s_person_count >= DB_STORE_MAX_PERSONS) {
            xSemaphoreGive(s_mutex);
            return DAL_ERR_NO_MEM;
        }
        s_persons[s_person_count++] = *person;
    }
    s_dirty = true;  /* 标记脏，等空闲时 flush */
    xSemaphoreGive(s_mutex);
    return DAL_OK;
}

dal_err_t db_person_get(uint32_t id, db_person_t *out)
{
    if (!s_inited || out == NULL || id == 0) return DAL_ERR_INVALID;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (uint16_t i = 0; i < s_person_count; i++) {
        if (s_persons[i].id == id) {
            *out = s_persons[i];
            xSemaphoreGive(s_mutex);
            return DAL_OK;
        }
    }
    xSemaphoreGive(s_mutex);
    return DAL_ERR_NOT_FOUND;
}

dal_err_t db_person_del(uint32_t id)
{
    if (!s_inited || id == 0) return DAL_ERR_INVALID;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (uint16_t i = 0; i < s_person_count; i++) {
        if (s_persons[i].id == id) {
            s_persons[i] = s_persons[s_person_count - 1];
            s_person_count--;
            s_dirty = true;
            xSemaphoreGive(s_mutex);
            return DAL_OK;
        }
    }
    xSemaphoreGive(s_mutex);
    return DAL_ERR_NOT_FOUND;
}

uint16_t db_person_list(db_person_t *out, uint16_t max_count)
{
    if (!s_inited || out == NULL) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint16_t n = (s_person_count < max_count) ? s_person_count : max_count;
    memcpy(out, s_persons, sizeof(db_person_t) * n);
    xSemaphoreGive(s_mutex);
    return n;
}

uint16_t db_person_count(void)
{
    return s_inited ? s_person_count : 0;
}

/* ---- 通行记录 ---- */

dal_err_t db_record_add(const db_record_t *record)
{
    if (!s_inited || record == NULL) return DAL_ERR_INVALID;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint16_t idx = s_record_count % DB_STORE_MAX_RECORDS;
    s_records[idx] = *record;
    s_record_count++;
    s_dirty = true;  /* 标记脏 */
    xSemaphoreGive(s_mutex);
    return DAL_OK;
}

uint16_t db_record_query_recent(db_record_t *out, uint16_t max_count)
{
    if (!s_inited || out == NULL || s_record_count == 0) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint16_t total = (s_record_count < DB_STORE_MAX_RECORDS)
                     ? s_record_count : DB_STORE_MAX_RECORDS;
    uint16_t n = (total < max_count) ? total : max_count;
    for (uint16_t i = 0; i < n; i++) {
        uint16_t src = (s_record_count - 1 - i) % DB_STORE_MAX_RECORDS;
        out[i] = s_records[src];
    }
    xSemaphoreGive(s_mutex);
    return n;
}

/* ---- 系统配置 KV ---- */

dal_err_t db_config_set_u32(const char *key, uint32_t value)
{
    if (!s_inited || key == NULL) return DAL_ERR_INVALID;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    char val[64];
    snprintf(val, sizeof(val), "%u", (unsigned)value);
    int idx = cfg_find(key);
    if (idx >= 0) {
        snprintf(s_cfg[idx].val, sizeof(s_cfg[idx].val), "%s", val);
    } else if (s_cfg_count < DB_CFG_MAX_KEYS) {
        snprintf(s_cfg[s_cfg_count].key, sizeof(s_cfg[s_cfg_count].key), "%s", key);
        snprintf(s_cfg[s_cfg_count].val, sizeof(s_cfg[s_cfg_count].val), "%s", val);
        s_cfg_count++;
    }
    s_dirty = true;
    xSemaphoreGive(s_mutex);
    return DAL_OK;
}

dal_err_t db_config_get_u32(const char *key, uint32_t *out, uint32_t def)
{
    if (!s_inited || key == NULL || out == NULL) return DAL_ERR_INVALID;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int idx = cfg_find(key);
    if (idx >= 0) {
        *out = (uint32_t)strtoul(s_cfg[idx].val, NULL, 10);
    } else {
        *out = def;
    }
    xSemaphoreGive(s_mutex);
    return DAL_OK;
}

dal_err_t db_config_set_str(const char *key, const char *value)
{
    if (!s_inited || key == NULL || value == NULL) return DAL_ERR_INVALID;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int idx = cfg_find(key);
    if (idx >= 0) {
        snprintf(s_cfg[idx].val, sizeof(s_cfg[idx].val), "%s", value);
    } else if (s_cfg_count < DB_CFG_MAX_KEYS) {
        snprintf(s_cfg[s_cfg_count].key, sizeof(s_cfg[s_cfg_count].key), "%s", key);
        snprintf(s_cfg[s_cfg_count].val, sizeof(s_cfg[s_cfg_count].val), "%s", value);
        s_cfg_count++;
    }
    s_dirty = true;
    xSemaphoreGive(s_mutex);
    return DAL_OK;
}

dal_err_t db_config_get_str(const char *key, char *out, uint16_t len, const char *def)
{
    if (!s_inited || key == NULL || out == NULL) return DAL_ERR_INVALID;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int idx = cfg_find(key);
    if (idx >= 0) {
        snprintf(out, len, "%s", s_cfg[idx].val);
    } else if (def != NULL) {
        snprintf(out, len, "%s", def);
    }
    xSemaphoreGive(s_mutex);
    return DAL_OK;
}
