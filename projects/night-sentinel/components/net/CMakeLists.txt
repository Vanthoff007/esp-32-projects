idf_component_register(SRCS "wifi.c" "wifi_portal.c" "ns_mdns.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server espressif__mdns)
