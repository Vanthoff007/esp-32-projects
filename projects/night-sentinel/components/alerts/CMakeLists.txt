idf_component_register(SRCS "audio.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES board esp_driver_i2s esp_timer
                       EMBED_FILES "assets/warn.pcm")
