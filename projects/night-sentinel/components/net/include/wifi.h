#pragma once
#include <stdint.h>
#include "esp_err.h"

// Join the network set in menuconfig (Night Sentinel Wi-Fi) and wait for an IP address.
// Needs nvs_flash_init() first. Returns ESP_ERR_TIMEOUT if there is no IP within timeout_ms;
// the driver keeps retrying in the background either way.
esp_err_t ns_wifi_connect(uint32_t timeout_ms);
