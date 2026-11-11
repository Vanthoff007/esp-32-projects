# Compiled only when this model is the one chosen in menuconfig (Platform: inference -> Model).
# WHOLE_ARCHIVE keeps the linker from dropping the file: nothing else refers to it by name,
# it registers itself (NS_MODEL_REGISTER in model.c).
# PRIV_REQUIRES: "infer" for the contract header, plus the runtime the model uses
# (e.g. esp-dl), which also goes into an idf_component.yml next to this file.
if(CONFIG_NS_MODEL_DUMMY)
    idf_component_register(SRCS "model.c"
                           PRIV_REQUIRES infer
                           WHOLE_ARCHIVE)
else()
    idf_component_register(PRIV_REQUIRES infer)
endif()
