#pragma once
#include "esp_err.h"

// Announce the board on the local network as <hostname>.local, with an HTTP service on
// port 80, so it can be opened at http://<hostname>.local/ instead of by IP address.
// Call after ns_wifi_connect() succeeds.
esp_err_t ns_mdns_start(const char *hostname);
