/**
 * @file    osal.h
 * @brief   OS Abstraction Layer - Umbrella header
 * @author  Access System Firmware Team
 * @version 1.0
 *
 * Provides unified OS API across FreeRTOS / RT-Thread / Linux.
 * All types are opaque handles to enable platform-independent code.
 */

#ifndef OSAL_H
#define OSAL_H

#include "osal_task.h"
#include "osal_mutex.h"
#include "osal_semaphore.h"
#include "osal_queue.h"
#include "osal_memory.h"
#include "osal_timer.h"

#endif /* OSAL_H */
