#pragma once
#include <stddef.h>
#include "esp_err.h"

// Development-only web server (turn off for the demo build: menuconfig -> Night Sentinel app):
//   /          page showing the live stream (the stream itself is on port 81)
//   /snap.jpg  one JPEG snapshot
//   /control   camera settings and IR, e.g. /control?var=ir&val=1
//   /status    JSON: IR, light, exposure, settings, plus main's fields (state, fps, ...)
//   /detect    POST a JPEG, get the detector's boxes back as JSON
//   /action    run a test hook, e.g. /action?name=alert_test
//   /sd        SD card: /sd?path=ev lists a folder, /sd?path=events.csv downloads a file
// The page itself is www/index.html (embedded at build time).
// Call after the camera is initialised and Wi-Fi has an IP.
esp_err_t ns_debug_http_start(void);

// Adds extra JSON fields to /status. Write `"key":value,"key2":value2` (no braces) into buf
// and return its length.
typedef int (*ns_debug_status_cb_t)(char *buf, size_t len);
void ns_debug_http_set_status_cb(ns_debug_status_cb_t cb);

// A test hook for /action?name=<name>&arg=<int>. Write a short reply into msg.
typedef esp_err_t (*ns_debug_action_t)(int arg, char *msg, size_t len);
void ns_debug_http_add_action(const char *name, ns_debug_action_t fn);
