#pragma once
// Frame hub: the one place that takes frames from the camera and shares them.
//
//   camera --> capture task (core 1) --+--> raw frame to the "sink" (the inference engine)
//                                      |
//                                      +--> copy --> encoder task (core 0) --> JPEG
//                                                       shared by /snap.jpg, live stream, recorder
//
// Each frame is grabbed once and turned into a JPEG at most once, however many users there
// are. Nothing runs while nobody wants frames: no grabbing, no encoding.
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_camera.h"
#include "esp_err.h"

// One encoded frame. Shared by reference count: whoever gets one must release it.
typedef struct {
    uint8_t *buf;
    size_t len;
    uint32_t seq;      // counts up by one per encoded frame
    int64_t t_us;      // when it was grabbed (esp_timer time)
    int width, height;
    atomic_int refs;
} ns_jpeg_t;

// The raw-frame user. Both functions are called from the capture task.
typedef struct {
    // Does it want the next frame? (false = inference is off, or the model isn't loaded)
    bool (*wanted)(void *ctx);
    // One frame. Only valid during the call. grab_us = how long the camera grab took.
    void (*on_frame)(const camera_fb_t *fb, int64_t grab_us, void *ctx);
    void *ctx;
} ns_frame_sink_t;

// Start the capture and encoder tasks. Call once, after ns_cam_init().
esp_err_t ns_hub_start(void);

// Set (or with NULL, remove) the raw-frame user. There is one.
void ns_hub_set_sink(const ns_frame_sink_t *sink);

// Newest JPEG with seq greater than after_seq, waiting up to timeout_ms for one.
// Pass after_seq = ns_hub_seq() to get a frame taken after this moment.
// Returns NULL on timeout. The caller must call ns_jpeg_release() when done.
// Calling this is also what tells the hub that JPEGs are wanted: it only grabs and encodes
// while somebody is waiting in this call (or was a moment ago).
ns_jpeg_t *ns_hub_get_jpeg(uint32_t after_seq, int timeout_ms);
void ns_jpeg_release(ns_jpeg_t *jpg);

// Sequence number of the newest JPEG so far (0 = none yet).
uint32_t ns_hub_seq(void);

// Grabs that returned no frame since boot.
uint32_t ns_hub_null_frames(void);
