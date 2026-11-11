#pragma once
// Phone push through ntfy: one HTTPS request per message, with an optional JPEG attached.
// Runs in its own task on core 0, so a slow TLS handshake never stalls the camera or the model.
// Server and topic: menuconfig -> Night Sentinel network.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    NS_PUSH_OK,        // delivered to the ntfy server
    NS_PUSH_FAILED,    // tried (with one retry) and failed, or no Wi-Fi
    NS_PUSH_SKIPPED,   // not tried: no topic configured, or the queue was full
} ns_push_status_t;

// Called exactly once per ns_notify_push(), from the notify task (or straight away from the
// caller's task when the push is skipped). ms = how long the push took.
typedef void (*ns_notify_done_t)(void *ctx, ns_push_status_t status, int ms);

// Start the notify task. Needs Wi-Fi started (it waits for a connection per push).
esp_err_t ns_notify_start(void);

// Queue a push and return at once.
//   filename, jpg, jpg_len: the attachment; jpg may be NULL for a text-only push.
//   The JPEG is not copied: it must stay valid until done() is called.
//   done may be NULL.
void ns_notify_push(const char *title, const char *message, const char *filename,
                    const uint8_t *jpg, size_t jpg_len, ns_notify_done_t done, void *ctx);

// Text-only push, for checking the phone setup. Blocks up to ~10 s. Returns the HTTP status
// (200 = delivered to ntfy) or -1.
int ns_notify_text(const char *title, const char *message);

bool ns_notify_configured(void);
