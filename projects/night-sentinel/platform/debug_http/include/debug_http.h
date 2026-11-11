#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"

// Development web server. Anyone on the same Wi-Fi can use it (no password), so turn it off
// for a build that leaves the house: menuconfig -> Night Sentinel app.
//   /          the page (given by the app with ns_debug_http_set_index)
//   /snap.jpg  one JPEG snapshot
//   /control   camera settings and IR, e.g. /control?var=ir&val=1
//   /reg       read or write one camera sensor register
//   /status    JSON: camera, IR, memory, Wi-Fi, SD card, plus fields added by other components
//   /action    run a named command, e.g. /action?name=reboot
//   /sd        SD card files: GET lists a folder or downloads a file, POST uploads, DELETE removes
//   :81/stream live MJPEG stream (its own server, so it never blocks the rest)
// Other components add their own routes, actions and status fields with the calls below,
// before ns_debug_http_start(). Call that after the camera is initialised and Wi-Fi has an IP.
esp_err_t ns_debug_http_start(void);

// The HTML for "/". Not copied: it must stay valid (e.g. a file embedded in the firmware).
void ns_debug_http_set_index(const char *html, size_t len);

// Extra JSON fields for /status. Write `"key":value,"key2":value2` (no braces, no comma at
// either end) into buf and return its length.
typedef int (*ns_debug_status_cb_t)(char *buf, size_t len);
void ns_debug_http_add_status(ns_debug_status_cb_t cb);

// A command for /action?name=<name>&arg=<int>. Write a short reply into msg.
typedef esp_err_t (*ns_debug_action_t)(int arg, char *msg, size_t len);
void ns_debug_http_add_action(const char *name, ns_debug_action_t fn);

// A whole extra URL on port 80, e.g. ns_debug_http_add_route("/detect", HTTP_POST, handler).
void ns_debug_http_add_route(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *));

// Runtime switch for the live stream (also /action?name=stream&arg=0|1). Off closes open
// viewers and refuses new ones. Snapshots are not affected.
void ns_debug_http_stream_enable(bool on);
bool ns_debug_http_stream_enabled(void);
