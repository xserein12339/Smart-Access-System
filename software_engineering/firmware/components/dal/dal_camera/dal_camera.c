/**
 * @file    dal_camera.c
 * @brief   摄像头设备 DAL 管理实现（基于 dal_registry）
 *
 * @author  xLumina
 * @version 1.0
 */
#include "dal_camera.h"
#include "dal_registry.h"

#ifndef DAL_CAMERA_MAX_INSTANCES
#define DAL_CAMERA_MAX_INSTANCES 2
#endif

static dal_registry_t        s_reg;
static dal_registry_entry_t  s_entries[DAL_CAMERA_MAX_INSTANCES];

dal_err_t dal_camera_register(const char *name,
                              const dal_camera_ops_t *ops,
                              void *ctx)
{
    if (s_reg.entries == NULL) {
        dal_err_t ret = dal_registry_init(&s_reg, s_entries,
                                          DAL_CAMERA_MAX_INSTANCES);
        if (ret != DAL_OK) return ret;
    }
    return dal_registry_register(&s_reg, name, ops, ctx);
}

dal_err_t dal_camera_get(const char *name,
                         const dal_camera_ops_t **ops,
                         void **ctx)
{
    return dal_registry_get(&s_reg, name, (const void **)ops, ctx);
}
