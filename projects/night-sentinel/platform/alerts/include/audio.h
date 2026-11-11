#pragma once
// Speaker warning through the MAX98357 amplifier (I2S on BCLK 45 / WS 46 / DOUT 42).
// A prerecorded 16 kHz mono clip (assets/warn.pcm, made by tools/make_audio.py) is embedded
// in the firmware; playback runs in its own task, so callers never wait.
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    CLIP_WARN,   // short beep, then "Warning. Person detected. You are being recorded."
    CLIP_BEEP,   // 300 ms 1 kHz beep only (for tests)
} ns_clip_t;

// Set up I2S and start the audio task (core 0). Safe to call when no speaker is attached.
esp_err_t ns_audio_init(void);

// Ask for a clip. Returns at once: false if a clip is already playing (the request is dropped,
// so a burst of alerts can never queue up repeats) or audio is disabled in menuconfig.
bool ns_audio_play(ns_clip_t clip);

bool ns_audio_busy(void);

// Volume in percent of the recorded level (menuconfig default). 0-400.
void ns_audio_set_gain(int percent);
int ns_audio_get_gain(void);
