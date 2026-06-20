/**
 * @file    dal_touch.c
 * @brief   触摸设备 DAL 管理实现（基于 dal_registry）
 *
 * @author  xLumina
 * @version 1.0
 */
#include "dal_touch.h"
#include "dal_registry.h"

#ifndef DAL_TOUCH_MAX_INSTANCES
#define DAL_TOUCH_MAX_INSTANCES 2
#endif

static dal_registry_t        s_reg;
static dal_registry_entry_t  s_entries[DAL_TOUCH_MAX_INSTANCES];

dal_err_t dal_touch_register(const char *name,
                             const dal_touch_ops_t *ops,
                             void *ctx)
{
    if (s_reg.entries == NULL) {
        dal_err_t ret = dal_registry_init(&s_reg, s_entries,
                                          DAL_TOUCH_MAX_INSTANCES);
        if (ret != DAL_OK) return ret;
    }
    return dal_registry_register(&s_reg, name, ops, ctx);
}

dal_err_t dal_touch_get(const char *name,
                        const dal_touch_ops_t **ops,
                        void **ctx)
{
    return dal_registry_get(&s_reg, name, (const void **)ops, ctx);
}
