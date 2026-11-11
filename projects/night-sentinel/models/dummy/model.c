// dummy: the smallest possible model, and the template for new ones.
// It needs no weights file and no neural network: it reports one "bright" box covering the
// brighter half of the frame (left or right). That is enough to test the whole pipeline
// (camera -> model -> results -> web page) without a real model.
//
// To start a new model: copy this folder, rename "dummy"/"DUMMY" everywhere in its three
// files, and replace the bodies of the functions below. See models/README.md.
#include "infer.h"

#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "dummy";

static float s_score = CONFIG_NS_DUMMY_SCORE_PCT / 100.0f;

// Set up the network from its weights and allocate buffers. weights is NULL here because
// .weights_file is NULL; a real model gets the whole file in memory (see infer.h).
static esp_err_t model_init(const uint8_t *weights, size_t len)
{
    ESP_LOGI(TAG, "nothing to load");
    return ESP_OK;
}

// Brightness of one RGB565 pixel, 0..255 (green carries most of it).
static int luma(const ns_frame_t *f, int x, int y)
{
    const uint8_t *p = f->data + 2 * (y * f->width + x);
    uint16_t px = f->format == NS_PIX_RGB565_BE ? (p[0] << 8) | p[1] : (p[1] << 8) | p[0];
    return ((px >> 5) & 0x3f) << 2;
}

// One frame in, results out. A real model does three steps here:
//   1. preprocess: resize / convert the frame to the network's input
//   2. run the network
//   3. postprocess: turn its output into ns_item_t entries
static esp_err_t model_run(const ns_frame_t *frame, ns_result_t *out)
{
    long left = 0, right = 0;
    for (int y = 0; y < frame->height; y += 8) {
        for (int x = 0; x < frame->width / 2; x += 8) {
            left += luma(frame, x, y);
            right += luma(frame, x + frame->width / 2, y);
        }
    }
    bool is_left = left >= right;
    out->count = 1;
    out->item[0] = (ns_item_t){
        .cls = 0,
        .score = s_score,
        .x1 = is_left ? 0 : frame->width / 2,
        .y1 = 0,
        .x2 = is_left ? frame->width / 2 - 1 : frame->width - 1,
        .y2 = frame->height - 1,
    };
    return ESP_OK;
}

// Optional: settings that can change while running.
static esp_err_t model_set_param(const char *key, float value)
{
    if (strcmp(key, "score_thr") != 0) return ESP_ERR_NOT_FOUND;
    s_score = value;
    return ESP_OK;
}

static const char *const s_labels[] = {"bright"};

static const ns_model_t dummy = {
    .name = "dummy",
    .task = NS_TASK_DETECT,
    .labels = s_labels,
    .n_labels = 1,
    .weights_file = NULL,   // e.g. "my_model.espdl": looked for in the SD card's models folder
    .init = model_init,
    .run = model_run,
    .set_param = model_set_param,
    .deinit = NULL,
};

NS_MODEL_REGISTER(dummy)
