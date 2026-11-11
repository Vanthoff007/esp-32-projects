#pragma once
// Night Sentinel, the app: what this project does with the model's results.
//
//   model results --> box filters --> intrusion state machine --> on an intrusion:
//                                         speaker + phone push + JPEG and CSV row on SD + video clip
//
// Everything below this layer (camera, SD, Wi-Fi, web server, recorder, inference engine) is
// the reusable platform; the model is whatever folder in models/ the build selected. The app
// only needs the model to have a label called "person".
#include "esp_err.h"
#include "ns_event.h"

#define NS_FW_VERSION "0.10.0"

// Hook the app into the platform: results listener, web page, test actions, heartbeat.
// Call once from app_main(), before ns_infer_start() and ns_debug_http_start().
esp_err_t sentinel_start(void);

// evidence.c: queue an event for the SD card. Its JPEG is written at once, its events.csv row
// once the phone push has finished (so the row can say whether the push worked), or after
// 25 s at the latest. Takes its own reference; the caller keeps theirs.
//   /sdcard/ev/E00042_20261110-221501.jpg   one JPEG per event
//   /sdcard/events.csv                      one row per event
void sentinel_save_event(ns_event_t *ev);
