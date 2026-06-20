/**
 * @file    dal_network.c
 * @brief   网络设备 DAL 管理实现（基于 dal_registry）
 *
 * @author  xLumina
 * @version 1.0
 */
#include "dal_network.h"
#include "dal_registry.h"

#ifndef DAL_NETWORK_MAX_INSTANCES
#define DAL_NETWORK_MAX_INSTANCES 1
#endif

static dal_registry_t        s_reg;
static dal_registry_entry_t  s_entries[DAL_NETWORK_MAX_INSTANCES];

dal_err_t dal_network_register(const char *name,
                               const dal_network_ops_t *ops,
                               void *ctx)
{
    if (s_reg.entries == NULL) {
        dal_err_t ret = dal_registry_init(&s_reg, s_entries,
                                          DAL_NETWORK_MAX_INSTANCES);
        if (ret != DAL_OK) return ret;
    }
    return dal_registry_register(&s_reg, name, ops, ctx);
}

dal_err_t dal_network_get(const char *name,
                          const dal_network_ops_t **ops,
                          void **ctx)
{
    return dal_registry_get(&s_reg, name, (const void **)ops, ctx);
}
