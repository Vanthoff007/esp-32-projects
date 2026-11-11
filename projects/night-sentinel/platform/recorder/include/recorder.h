#pragma once
// Video recorder: saves camera frames to the SD card as .avi files (Motion-JPEG, plays in VLC).
// It takes its JPEGs from the frame hub, so it works with inference on or off and shares one
// JPEG encode with the live stream.
//
//   /sdcard/rec/20261111-221501_V.avi        continuous: one file per minute
//   /sdcard/rec/20261111-221733_E00042.avi   event clip: a few seconds before the trigger + after
//
// When the card runs low, the oldest files in /sdcard/rec are deleted to make room. Nothing
// outside that folder is ever deleted.
// Settings (frame rate, clip length, minimum free space): menuconfig -> Platform: recorder.
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    NS_REC_OFF,          // nothing is recorded
    NS_REC_CONTINUOUS,   // record all the time
    NS_REC_EVENT,        // record only around ns_rec_trigger() calls
} ns_rec_mode_t;

// Start the recorder task and add its web controls (/action?name=rec&arg=0|1|2, /status
// fields). Call once, after ns_hub_start() and ns_storage_start(). The mode starts as set in
// menuconfig. Does nothing if the recorder is switched off in menuconfig.
esp_err_t ns_rec_start(void);

// Runtime switch. Changing the mode closes the file being written.
void ns_rec_set_mode(ns_rec_mode_t mode);
ns_rec_mode_t ns_rec_get_mode(void);

// In NS_REC_EVENT mode: save a clip now. tag goes into the file name (letters and digits,
// e.g. "E00042"). A trigger during a clip makes that clip longer. Ignored in the other modes.
void ns_rec_trigger(const char *tag);

// True while a file is being written.
bool ns_rec_active(void);
