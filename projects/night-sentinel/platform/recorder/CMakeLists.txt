idf_component_register(SRCS "recorder.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES camera esp32-camera storage debug_http esp_timer)
