idf_component_register(SRCS "light.c" "camera.c" "night.c" "frame_hub.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp32-camera
                       PRIV_REQUIRES board esp_driver_i2c esp_timer)
