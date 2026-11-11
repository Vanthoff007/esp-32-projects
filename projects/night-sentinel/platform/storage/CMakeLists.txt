idf_component_register(SRCS "storage.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES board fatfs sdmmc esp_driver_sdspi esp_driver_spi esp_timer)
