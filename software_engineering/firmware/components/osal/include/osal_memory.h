/**
 * @file    osal_memory.h
 * @brief   OSAL Memory Management API
 *
 * Wraps ESP-IDF heap_caps_malloc with capability-based allocation.
 * Provides unified interface for internal SRAM and external PSRAM.
 */

#ifndef OSAL_MEMORY_H
#define OSAL_MEMORY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Memory capability flags ---- */
#define OSAL_MEM_CAP_INTERNAL  0x01    /* Internal SRAM */
#define OSAL_MEM_CAP_PSRAM     0x02    /* External PSRAM */
#define OSAL_MEM_CAP_DMA       0x04    /* DMA-capable memory */
#define OSAL_MEM_CAP_DEFAULT   0x08    /* Default allocation */

/**
 * @brief Allocate memory with specific capabilities
 *
 * @param size Size in bytes
 * @param caps Bitmask of OSAL_MEM_CAP_* flags
 * @return Pointer to allocated memory, or NULL on failure
 */
void *osal_malloc_caps(size_t size, uint32_t caps);

/**
 * @brief Allocate zero-initialized memory with specific capabilities
 *
 * @param n    Number of elements
 * @param size Size of each element
 * @param caps Bitmask of OSAL_MEM_CAP_* flags
 * @return Pointer to allocated memory, or NULL on failure
 */
void *osal_calloc_caps(size_t n, size_t size, uint32_t caps);

/**
 * @brief Allocate from internal SRAM (fast, limited size)
 *
 * @param size Size in bytes
 * @return Pointer to allocated memory, or NULL
 */
void *osal_malloc_internal(size_t size);

/**
 * @brief Allocate from external PSRAM (large, slower)
 *
 * @param size Size in bytes
 * @return Pointer to allocated memory, or NULL
 */
void *osal_malloc_psram(size_t size);

/**
 * @brief Free allocated memory
 *
 * @param ptr Pointer returned by any osal_malloc* / osal_calloc*
 */
void osal_free(void *ptr);

/**
 * @brief Get total free heap size
 *
 * @param caps Bitmask of OSAL_MEM_CAP_* flags
 * @return Free size in bytes
 */
size_t osal_get_free_heap_size(uint32_t caps);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_MEMORY_H */
