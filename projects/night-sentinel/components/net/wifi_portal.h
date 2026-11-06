#pragma once
#include <stdbool.h>
#include <stdint.h>

// Private to the net component (it lives next to the .c files, not in include/),
// so only wifi.c and wifi_portal.c can use it.

// One network's login. +1 on each size leaves room for the '\0' that ends a C string:
// SSIDs are at most 32 characters, WPA passwords at most 64.
typedef struct {
    char ssid[33];
    char pass[65];
} ns_wifi_creds_t;

// Scan nearby networks and start the setup web page on http://192.168.4.1/.
// The setup hotspot (AP) must already be configured.
void ns_portal_start(void);
void ns_portal_stop(void);

// Wait up to timeout_ms for someone to submit the form. Returns true and fills *out if they did.
bool ns_portal_wait_creds(ns_wifi_creds_t *out, uint32_t timeout_ms);

// Message shown at the top of the page, e.g. why the last attempt failed. "" hides it.
void ns_portal_set_status(const char *msg);

// While true, the page won't rescan: a scan would break a connection attempt in progress.
void ns_portal_set_busy(bool busy);
