/**
 * @file    osal_memory.c
 * @brief   OSAL Memory implementation (ESP-IDF heap_caps backend)
 */

#include "osal_memory.h"

#include "esp_heap_caps.h"

#include <string.h>

void *osal_malloc_caps(size_t size, uint32_t caps)
{
    uint32_t esp_caps = 0;

    if (caps & OSAL_MEM_CAP_INTERNAL) {
        esp_caps |= MALLOC_CAP_INTERNAL;
    }
    if (caps & OSAL_MEM_CAP_PSRAM) {
        esp_caps |= MALLOC_CAP_SPIRAM;
    }
    if (caps & OSAL_MEM_CAP_DMA) {
        esp_caps |= MALLOC_CAP_DMA;
    }
    if (caps & OSAL_MEM_CAP_DEFAULT) {
        esp_caps |= MALLOC_CAP_DEFAULT;
    }

    /* If no specific caps given, use default */
    if (esp_caps == 0) {
        esp_caps = MALLOC_CAP_DEFAULT;
    }

    return heap_caps_malloc(size, esp_caps);
}

void *osal_calloc_caps(size_t n, size_t size, uint32_t caps)
{
    size_t total = n * size;
    void *ptr = osal_malloc_caps(total, caps);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *osal_malloc_internal(size_t size)
{
    return osal_malloc_caps(size, OSAL_MEM_CAP_INTERNAL);
}

void *osal_malloc_psram(size_t size)
{
    return osal_malloc_caps(size, OSAL_MEM_CAP_PSRAM);
}

void osal_free(void *ptr)
{
    if (ptr != NULL) {
        heap_caps_free(ptr);
    }
}

size_t osal_get_free_heap_size(uint32_t caps)
{
    uint32_t esp_caps = MALLOC_CAP_DEFAULT;

    if (caps & OSAL_MEM_CAP_INTERNAL) {
        esp_caps |= MALLOC_CAP_INTERNAL;
    }
    if (caps & OSAL_MEM_CAP_PSRAM) {
        esp_caps |= MALLOC_CAP_SPIRAM;
    }
    if (caps & OSAL_MEM_CAP_DMA) {
        esp_caps |= MALLOC_CAP_DMA;
    }

    return heap_caps_get_free_size(esp_caps);
}
