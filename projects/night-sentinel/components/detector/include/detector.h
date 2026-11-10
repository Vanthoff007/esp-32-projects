#pragma once
// Person detector: a C front door to esp-dl's C++ pedestrian_detect model.
// The rest of the firmware is C and only ever includes this header. All C++ stays in detector.cpp.
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// The C++ compiler also reads this header (from detector.cpp). extern "C" tells it to keep the
// plain C function names, so the .c files can link against them.
#ifdef __cplusplus
extern "C" {
#endif

// The model returns at most 10 boxes per frame.
#define NS_DET_MAX_BOXES 10

// One detected person. Box corners are in pixels of the input image:
// (x1, y1) = top-left, (x2, y2) = bottom-right.
typedef struct {
    float score;  // 0..1, how sure the model is that this is a person
    int x1, y1, x2, y2;
} ns_det_box_t;

// All people found in one frame, highest score first.
typedef struct {
    int count;                          // how many entries of box[] are filled
    ns_det_box_t box[NS_DET_MAX_BOXES];
    int64_t time_us;                    // how long ns_det_run() took
} ns_det_results_t;

// Load the model from flash into PSRAM. Call once at boot, before ns_det_run().
// score_thr: boxes below this score are dropped (the model's own default is 0.7).
esp_err_t ns_det_init(float score_thr);

// Run the model on one RGB565 image (any size; esp-dl resizes it to 224x224 itself).
// big_endian: true for frames straight from esp32-camera, false for most JPEG decoders.
// Safe to call from several tasks: calls take turns.
esp_err_t ns_det_run(const uint16_t *rgb565, int w, int h, bool big_endian, ns_det_results_t *out);

// Decode a colour JPEG to RGB565 (big-endian, like the camera) and run the model on it.
// Used by POST /detect to score saved test images. *w and *h get the decoded size (may be NULL).
esp_err_t ns_det_run_jpeg(const uint8_t *jpg, size_t len, ns_det_results_t *out, int *w, int *h);

// Change the score threshold after init.
void ns_det_set_score_thr(float score_thr);

#ifdef __cplusplus
}
#endif
