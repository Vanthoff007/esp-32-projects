#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// Start SNTP (internet time) in the background and set the time zone from menuconfig.
// Call once, after Wi-Fi is up. Until the first sync the clock reads 1970.
void ns_time_start(void);

// True once the clock has been set from the internet.
bool ns_time_synced(void);

// "2026-11-10T22:15:01+05:30", or "uptime 123.4 s" if the clock isn't set yet.
void ns_time_iso(time_t t, char *buf, size_t len);
