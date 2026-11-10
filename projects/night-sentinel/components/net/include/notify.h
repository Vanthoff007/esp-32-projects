#pragma once
// Phone push through ntfy (plan section 11): one HTTPS PUT per event with the JPEG attached.
// Runs in its own task on core 0, so a slow TLS handshake never stalls detection.
#include <stdbool.h>
#include "esp_err.h"
#include "ns_event.h"

// Start the notify task. Needs Wi-Fi started (it waits for a connection per event).
esp_err_t ns_notify_start(void);

// Queue an event for a push. Takes its own reference; the caller keeps theirs.
// Sets ev->push_result to PUSH_SKIPPED at once if no topic is configured or the queue is full.
void ns_notify_send(ns_event_t *ev);

// Text-only push, for checking the phone setup. Blocks up to ~10 s. Returns the HTTP status
// (200 = delivered to ntfy) or -1.
int ns_notify_text(const char *title, const char *message);

bool ns_notify_configured(void);
