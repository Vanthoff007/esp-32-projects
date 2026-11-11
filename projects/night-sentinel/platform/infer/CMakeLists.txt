idf_component_register(SRCS "infer.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES camera esp32-camera debug_http esp_timer espressif__esp_new_jpeg)
