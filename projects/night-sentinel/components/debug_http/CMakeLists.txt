idf_component_register(SRCS "debug_http.c"
                       INCLUDE_DIRS "include"
                       EMBED_TXTFILES "www/index.html"
                       PRIV_REQUIRES esp_http_server camera esp32-camera board detector esp_psram storage)
