# Compiled only when this model is the one chosen in menuconfig (Platform: inference -> Model).
# WHOLE_ARCHIVE keeps the linker from dropping the file: nothing else refers to it by name,
# it registers itself (NS_MODEL_REGISTER in model.cpp).
if(CONFIG_NS_MODEL_PEDESTRIAN_PICO)
    idf_component_register(SRCS "model.cpp"
                           PRIV_REQUIRES infer esp-dl
                           WHOLE_ARCHIVE)
else()
    idf_component_register(PRIV_REQUIRES infer esp-dl)
endif()
