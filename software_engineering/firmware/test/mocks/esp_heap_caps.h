/**
 * @file    esp_heap_caps.h
 * @brief   Mock esp_heap_caps.h — 堆内存能力分配（宿主机测试用）
 *
 * @details 转发到标准 malloc/free，忽略能力标志。使 service_camera 等
 *          使用 PSRAM 分配的代码可在 host 测试。
 *
 * @author  xLumina
 * @version 1.0
 */
#ifndef MOCK_ESP_HEAP_CAPS_H
#define MOCK_ESP_HEAP_CAPS_H

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MALLOC_CAP_8BIT     (1 << 0)
#define MALLOC_CAP_32BIT    (1 << 1)
#define MALLOC_CAP_DMA      (1 << 2)
#define MALLOC_CAP_SPIRAM   (1 << 3)
#define MALLOC_CAP_INTERNAL (1 << 4)

static inline void *heap_caps_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    return malloc(size);
}

static inline void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    (void)caps;
    return calloc(n, size);
}

static inline void heap_caps_free(void *ptr)
{
    free(ptr);
}

#ifdef __cplusplus
}
#endif
#endif /* MOCK_ESP_HEAP_CAPS_H */
