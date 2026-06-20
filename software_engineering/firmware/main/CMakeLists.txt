idf_component_register(SRCS "main.c"

                    INCLUDE_DIRS "."

                    REQUIRES dal
                              osal
                              pal
                              bsp
                              middleware
                              lvgl__lvgl)
