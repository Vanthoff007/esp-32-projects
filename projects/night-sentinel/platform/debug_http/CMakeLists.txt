idf_component_register(SRCS "debug_http.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server
                       PRIV_REQUIRES camera esp32-camera board esp_psram storage esp_wifi esp_timer)
