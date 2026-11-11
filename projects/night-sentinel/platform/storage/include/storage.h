#pragma once
// microSD card (SPI mode), mounted at /sdcard. Card writes run on their own task, so a slow
// card never stalls the camera or the model.
// A missing or failing card is never fatal: writes are skipped with a warning, and the card
// is retried (remounted) before the next write.
//
// This component knows nothing about what is stored. Folders used by the rest of the firmware:
//   /sdcard/models/   model weight files (read by the inference engine)
//   /sdcard/rec/      video recordings (written by the recorder)
// Everything else on the card belongs to the app.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define NS_STORAGE_MOUNT "/sdcard"

// Mount the card and start the storage task. Returns ESP_OK even without a card.
esp_err_t ns_storage_start(void);

bool ns_storage_mounted(void);

// Append one line (no newline needed) to a CSV file in the card's root, e.g.
// ns_storage_append("heartbeat.csv", header, line). The header is written when the file is new.
// Returns at once; the storage task does the write.
void ns_storage_append(const char *file, const char *header, const char *line);

// Run job(arg) on the storage task, after the writes already waiting. Use it for work that
// needs several card writes in a row. Returns false if the queue is full (job is not run).
typedef void (*ns_storage_job_t)(void *arg);
bool ns_storage_run(ns_storage_job_t job, void *arg);

// The two calls below block until the data is on the card. Only call them from inside a job
// given to ns_storage_run(). Paths are relative to the card, e.g. "ev/E00042.jpg".

// Write a whole file. Creates the folder if it is missing. *write_ms (may be NULL) gets the
// time taken, or -1 if nothing was written.
bool ns_storage_write_file(const char *rel_path, const void *data, size_t len, int *write_ms);

// Append one line to a file, writing the header first if the file is new.
bool ns_storage_append_now(const char *rel_path, const char *header, const char *line);

// Card size and free space in MB (0 if not mounted).
void ns_storage_space(uint32_t *total_mb, uint32_t *free_mb);

// ns_storage_write_file() statistics: the last write time in ms and the median of the recent
// ones, plus counts since boot.
void ns_storage_write_stats(int *last_ms, int *median_ms, uint32_t *writes, uint32_t *failures);
