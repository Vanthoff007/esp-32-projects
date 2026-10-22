idf_component_register(SRCS "wifi.c" "ns_mdns.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event espressif__mdns)
