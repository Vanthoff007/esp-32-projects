idf_component_register(SRCS "sentinel.c" "evidence.c"
                       INCLUDE_DIRS "include"
                       EMBED_TXTFILES "www/index.html"
                       REQUIRES event
                       PRIV_REQUIRES intrusion infer camera esp32-camera board storage net alerts
                                     debug_http recorder esp_timer esp_wifi)
