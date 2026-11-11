#pragma once
// THE CONTRACT between the firmware and a model.
//
// A model is one folder in models/. It fills in one ns_model_t and registers it with
// NS_MODEL_REGISTER(). The firmware never includes anything from a model folder, and a model
// includes nothing from the firmware except this header. So a model can be added, replaced or
// deleted without touching any other file.
//
//   camera frame --> model.run() --> ns_result_t --> the app (e.g. the intrusion logic)
//
// Everything between the frame and the result is the model's own business: resizing,
// normalising, the neural network, decoding its output, and which runtime it uses.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// The C++ compiler also reads this header (model files are usually C++). extern "C" keeps
// the plain C function names, so C and C++ files can call each other.
#ifdef __cplusplus
extern "C" {
#endif

// ---- what goes into a model ------------------------------------------------------------

typedef enum {
    NS_PIX_RGB565_BE,   // 2 bytes per pixel, high byte first: frames straight from the camera
    NS_PIX_RGB565_LE,   // 2 bytes per pixel, low byte first
} ns_pix_t;

typedef struct {
    const uint8_t *data;   // pixels, row by row from the top-left; only valid during the call
    size_t len;            // bytes in data
    int width, height;     // 320 x 240 from the camera; any size from POST /detect
    ns_pix_t format;
} ns_frame_t;

// ---- what comes out --------------------------------------------------------------------

typedef enum {
    NS_TASK_DETECT,     // finds objects: every item has a box
    NS_TASK_CLASSIFY,   // labels the whole image: items have no box (all four corners 0)
} ns_task_t;

#define NS_INFER_MAX_ITEMS 10

typedef struct {
    int cls;              // index into the model's labels[]
    float score;          // 0..1, how sure the model is
    int x1, y1, x2, y2;   // box in pixels of the input frame: top-left and bottom-right corner
} ns_item_t;

typedef struct {
    int count;                            // how many entries of item[] are filled
    ns_item_t item[NS_INFER_MAX_ITEMS];   // highest score first
    int64_t time_us;                      // how long run() took (filled in by the engine)
    uint32_t t_ms;                        // when the frame was taken, ms since boot (engine)
} ns_result_t;

// ---- a model ---------------------------------------------------------------------------

typedef struct {
    const char *name;             // "pedestrian_pico": shown on the web page and in logs
    ns_task_t task;
    const char *const *labels;    // class names, e.g. {"person"}
    int n_labels;
    const char *weights_file;     // file name inside the models folder on the SD card;
                                  // NULL if the model needs no file

    // Load the model. weights / len = the contents of weights_file, already read from the SD
    // card into PSRAM by the engine (NULL / 0 if the model has no file). The buffer is
    // 16-byte aligned and stays valid for as long as the model is loaded.
    // Called from the capture task, again after a failure when the user presses Start.
    esp_err_t (*init)(const uint8_t *weights, size_t len);

    // Run on one frame and fill in out->count and out->item[]. Never called by two tasks at
    // the same time, and never before init() succeeded.
    esp_err_t (*run)(const ns_frame_t *frame, ns_result_t *out);

    // Optional (may be NULL): change a setting, e.g. set_param("score_thr", 0.3f).
    // May be called before init(); the model must then remember the value for init().
    esp_err_t (*set_param)(const char *key, float value);

    // Optional (may be NULL): free everything init() allocated.
    void (*deinit)(void);
} ns_model_t;

// Put this once in the model's source file, after the ns_model_t. It registers the model
// before app_main() starts, so nothing else has to know the model exists.
#define NS_MODEL_REGISTER(model)                                          \
    __attribute__((constructor)) static void ns_model_register_##model(void) \
    {                                                                     \
        ns_infer_register(&model);                                        \
    }
void ns_infer_register(const ns_model_t *model);

// ---- the engine (used by the app and the web server, not by models) ---------------------

// Connect the engine to the camera's frame hub and to the web server (POST /detect, the
// "detect" action, /status fields). Call once, after ns_hub_start() and ns_storage_start().
esp_err_t ns_infer_start(void);

// The model built into this firmware, or NULL if the build has none.
const ns_model_t *ns_infer_model(void);

// Index of a label in the model's labels[], or -1 (also -1 when there is no model).
int ns_infer_label_index(const char *label);

// Runtime switch. Off = the model is not run and costs no CPU; camera, stream, recording
// and everything else keep working. Starts as set in menuconfig.
void ns_infer_enable(bool on);
bool ns_infer_enabled(void);

// True once the model is loaded. If it isn't, ns_infer_message() says why.
bool ns_infer_ready(void);
const char *ns_infer_message(void);

// Pass a setting to the model (see set_param above).
esp_err_t ns_infer_set_param(const char *key, float value);

// Who gets the results. Both functions are called from the capture task.
typedef struct {
    // One call per frame while inference runs. frame is only valid during the call.
    void (*on_result)(const ns_frame_t *frame, const ns_result_t *res, void *ctx);
    // Inference started (true) or stopped (false): switched from the web page, or the model
    // became ready. May be NULL.
    void (*on_running)(bool running, void *ctx);
    void *ctx;
} ns_infer_listener_t;
void ns_infer_set_listener(const ns_infer_listener_t *listener);

typedef struct {
    float fps;         // loop rate, averaged over the last 50 frames (0 while stopped)
    float infer_ms;    // average model time over the same frames
    uint32_t frames;   // frames processed since boot
} ns_infer_stats_t;
void ns_infer_get_stats(ns_infer_stats_t *stats);

// Run the model on any frame, from any task (calls take turns with the live loop).
esp_err_t ns_infer_run(const ns_frame_t *frame, ns_result_t *out);

// Decode a colour JPEG to RGB565 (big-endian, like the camera) and run the model on it.
// *w and *h get the decoded size (may be NULL). Used by POST /detect.
esp_err_t ns_infer_run_jpeg(const uint8_t *jpg, size_t len, ns_result_t *out, int *w, int *h);

#ifdef __cplusplus
}
#endif
