#pragma once
#include "esp_err.h"

// Development-only web server (turn off for the demo build):
//   /          page showing the live stream
//   /stream    MJPEG live video (browser, VLC, OpenCV)
//   /snap.jpg  one JPEG snapshot
// Call after the camera is initialised and Wi-Fi has an IP.
esp_err_t ns_debug_http_start(void);
