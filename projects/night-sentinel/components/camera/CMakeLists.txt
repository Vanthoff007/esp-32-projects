idf_component_register(SRCS "light.c" "camera.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp32-camera
                       PRIV_REQUIRES board esp_driver_i2c)
