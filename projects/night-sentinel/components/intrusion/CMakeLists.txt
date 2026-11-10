# Pure C, no ESP-IDF dependencies on purpose: host_test/ builds the same file on the PC.
idf_component_register(SRCS "intrusion_fsm.c"
                       INCLUDE_DIRS "include")
