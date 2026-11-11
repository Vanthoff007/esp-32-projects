idf_component_register(SRCS "app_main.c"
                       PRIV_REQUIRES board camera esp32-camera nvs_flash net debug_http storage alerts
                                     infer recorder sentinel)
