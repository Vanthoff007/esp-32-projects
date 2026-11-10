// C++ side of the detector. Only this file touches esp-dl; see include/detector.h for the API.
#include "detector.h"

#include <new>  // std::nothrow

#include "dl_image_jpeg.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "pedestrian_detect.hpp"

static const char *TAG = "detector";


static PedestrianDetect *s_det = nullptr;    // the model object, created once in ns_det_init()
static SemaphoreHandle_t s_lock = nullptr;   // one ns_det_run() at a time (the model isn't thread-safe)

extern "C" esp_err_t ns_det_init(float score_thr)
{
    if (s_det) {
        return ESP_OK;  // already loaded
    }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    int64_t t0 = esp_timer_get_time();
    // lazy_load = false: load the model now, so a failure shows up at boot, not on the first frame.
    s_det = new (std::nothrow) PedestrianDetect(PedestrianDetect::PICO_S8_V1, false);
    if (!s_det) {
        ESP_LOGE(TAG, "out of memory creating the model");
        return ESP_ERR_NO_MEM;
    }
    s_det->set_score_thr(score_thr);
    ESP_LOGI(TAG, "model loaded in %lld ms, score threshold %.2f",
             (esp_timer_get_time() - t0) / 1000, score_thr);
    return ESP_OK;
}

extern "C" void ns_det_set_score_thr(float score_thr)
{
    if (!s_det) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_det->set_score_thr(score_thr);
    xSemaphoreGive(s_lock);
}

extern "C" esp_err_t ns_det_run(const uint16_t *rgb565, int w, int h, bool big_endian,
                                ns_det_results_t *out)
{
    if (!s_det) {
        return ESP_ERR_INVALID_STATE;  // ns_det_init() wasn't called
    }
    if (!rgb565 || !out || w <= 0 || h <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Describe the caller's pixels to esp-dl. Nothing is copied; esp-dl only reads them.
    dl::image::img_t img = {};
    img.data = (void *)rgb565;
    img.width = (uint16_t)w;
    img.height = (uint16_t)h;
    img.pix_type = big_endian ? dl::image::DL_IMAGE_PIX_TYPE_RGB565BE
                              : dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t t0 = esp_timer_get_time();
    std::list<dl::detect::result_t> &res = s_det->run(img);

    // Copy the C++ list into the plain C struct while we still hold the lock:
    // the next run() overwrites the list.
    out->count = 0;
    for (const auto &r : res) {
        if (out->count >= NS_DET_MAX_BOXES) {
            break;
        }
        ns_det_box_t *b = &out->box[out->count++];
        b->score = r.score;
        b->x1 = r.box[0];
        b->y1 = r.box[1];
        b->x2 = r.box[2];
        b->y2 = r.box[3];
    }
    out->time_us = esp_timer_get_time() - t0;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

extern "C" esp_err_t ns_det_run_jpeg(const uint8_t *jpg, size_t len, ns_det_results_t *out, int *w, int *h)
{
    if (!jpg || len == 0 || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    dl::image::jpeg_img_t jpeg = {};
    jpeg.data = (void *)jpg;
    jpeg.data_len = len;
    // Decode into the camera's own format, so this tests exactly what live frames go through.
    dl::image::img_t img = dl::image::sw_decode_jpeg(jpeg, dl::image::DL_IMAGE_PIX_TYPE_RGB565BE);
    if (!img.data) {
        return ESP_FAIL;   // not a JPEG, or a greyscale JPEG (the decoder only does colour)
    }
    if (w) *w = img.width;
    if (h) *h = img.height;
    esp_err_t err = ns_det_run((const uint16_t *)img.data, img.width, img.height, true, out);
    heap_caps_free(img.data);   // the decoder allocated it
    return err;
}
