idf_component_register(SRCS "app_main.c"
                       PRIV_REQUIRES board camera esp32-camera esp_timer nvs_flash net debug_http
                                     detector intrusion event alerts storage esp_wifi esp_psram)
