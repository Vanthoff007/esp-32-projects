#pragma once
// One intrusion event, shared by the tasks that act on it (speaker, phone push, SD card).
//
// The detect task creates it with one JPEG of the confirming frame. Every task that keeps it
// calls ns_event_retain() first and ns_event_release() when done; the last release frees the
// JPEG and the event. So there is one JPEG encode per event however many tasks use it.
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef enum { PUSH_PENDING = 0, PUSH_OK = 1, PUSH_FAILED = -1, PUSH_SKIPPED = -2 } ns_push_result_t;

typedef struct {
    uint32_t id;          // E00042: counts up forever (kept in NVS across reboots)
    int64_t uptime_ms;    // when it was confirmed, ms since boot
    time_t wall;          // the same moment as calendar time; 0 if the clock isn't set yet
    float score;          // best box score on the confirming frame
    int box[4];           // that box: x1, y1, x2, y2
    bool ir;              // IR LEDs were on
    float lux;            // light sensor reading (-1 if unknown)
    bool test;            // made by a test command, not by a real detection

    uint8_t *jpg;         // JPEG of the confirming frame (NULL if encoding failed)
    size_t jpg_len;

    volatile int push_result;   // ns_push_result_t, set by the notify task
    volatile int push_ms;       // how long the push took

    atomic_int refs;      // owners still using this event
} ns_event_t;

// New event with refs = 1 and the next id. Takes ownership of jpg (malloc'd; may be NULL).
ns_event_t *ns_event_new(uint8_t *jpg, size_t jpg_len);

void ns_event_retain(ns_event_t *ev);
void ns_event_release(ns_event_t *ev);

// "E00042"
void ns_event_name(const ns_event_t *ev, char *buf, size_t len);

// Total events since the counter was created (the next id minus one).
uint32_t ns_event_count(void);
