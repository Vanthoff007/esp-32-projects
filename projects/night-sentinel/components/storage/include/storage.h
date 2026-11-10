#pragma once
// microSD evidence (plan section 13), on its own task so card writes never stall detection.
//   /sdcard/ev/E00042_20261110-221501.jpg   one JPEG per event
//   /sdcard/events.csv                      one row per event
//   /sdcard/heartbeat.csv                   one row per heartbeat (soak test)
//   /sdcard/boot.csv                        one row per boot, with the reset reason
// A missing or failing card is never fatal: writes are skipped with a warning, and the card
// is retried (remounted) before the next write.
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "ns_event.h"

// Mount the card and start the storage task. Returns ESP_OK even without a card.
esp_err_t ns_storage_start(void);

bool ns_storage_mounted(void);

// Queue an event: its JPEG is written at once, its CSV row once the phone push has finished
// (so the row can say whether the push worked), or after 25 s at the latest.
void ns_storage_save_event(ns_event_t *ev);

// Append one line (no newline needed) to a CSV file in the card's root, e.g.
// ns_storage_append("heartbeat.csv", header, line). The header is written when the file is new.
void ns_storage_append(const char *file, const char *header, const char *line);

// Card size and free space in MB (0 if not mounted).
void ns_storage_space(uint32_t *total_mb, uint32_t *free_mb);

// Last JPEG write time in ms and the median of the recent ones (Day 9: <= 300 ms target).
void ns_storage_write_stats(int *last_ms, int *median_ms, uint32_t *writes, uint32_t *failures);
