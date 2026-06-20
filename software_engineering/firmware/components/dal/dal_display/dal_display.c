/**
 * @file    dal_display.c
 * @brief   显示设备 DAL 管理实现（基于 dal_registry）
 *
 * @author  xLumina
 * @version 1.0
 */
#include "dal_display.h"
#include "dal_registry.h"

#ifndef DAL_DISPLAY_MAX_INSTANCES
#define DAL_DISPLAY_MAX_INSTANCES 2
#endif

static dal_registry_t        s_reg;
static dal_registry_entry_t  s_entries[DAL_DISPLAY_MAX_INSTANCES];

dal_err_t dal_display_register(const char *name,
                               const dal_display_ops_t *ops,
                               void *ctx)
{
    /* 首次注册时初始化注册表（懒初始化，单线程启动模型下安全） */
    if (s_reg.entries == NULL) {
        dal_err_t ret = dal_registry_init(&s_reg, s_entries,
                                          DAL_DISPLAY_MAX_INSTANCES);
        if (ret != DAL_OK) return ret;
    }
    return dal_registry_register(&s_reg, name, ops, ctx);
}

dal_err_t dal_display_get(const char *name,
                          const dal_display_ops_t **ops,
                          void **ctx)
{
    return dal_registry_get(&s_reg, name, (const void **)ops, ctx);
}
