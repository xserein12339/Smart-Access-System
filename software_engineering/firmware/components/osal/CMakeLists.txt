idf_component_register(SRCS "osal_task.c"
                            "osal_mutex.c"
                            "osal_semaphore.c"
                            "osal_queue.c"
                            "osal_memory.c"
                            "osal_timer.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_timer)
