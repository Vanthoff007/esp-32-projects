#include "esp_log.h"
#include "mdns.h"
#include "ns_mdns.h"

// Source: espressif/mdns 1.14.0 README and include/mdns.h.

static const char *TAG = "mdns";

esp_err_t ns_mdns_start(const char *hostname)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return err;
    }
    mdns_hostname_set(hostname);                        // answers <hostname>.local
    mdns_instance_name_set("night-sentinel camera");    // friendly name in network browsers
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "open http://%s.local/", hostname);
    return ESP_OK;
}
