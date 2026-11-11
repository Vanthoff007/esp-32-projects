#pragma once
#include <stdint.h>
#include "esp_err.h"

// Get the board online and wait for an IP address. Needs nvs_flash_init() first.
// 1. Tries the network saved from the setup page (or, if none, the menuconfig one).
// 2. If that fails, opens the setup hotspot (menuconfig -> Night Sentinel network) and serves
//    http://192.168.4.1/, which lists nearby networks and asks for a password.
// Blocks until connected, then returns ESP_OK. Reconnects on its own after any later drop.
// timeout_ms limits each single connection attempt.
esp_err_t ns_wifi_connect(uint32_t timeout_ms);
