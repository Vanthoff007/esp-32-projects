// pedestrian_pico: Espressif's pre-trained pedestrian detector (ESPDet-Pico, 224x224 input,
// int8), run with ESP-DL. One class: "person".
// Weights: weights/pedestrian_detect_pico_s8_v1.espdl, copied to the SD card's models folder.
// Source of the weights and of the settings below: the espressif/pedestrian_detect component
// (pedestrian_detect.cpp), which this file replaces so the weights can come from the SD card.
#include "infer.h"

#include <list>
#include <new>      // std::nothrow
#include <string.h>

#include "dl_detect_base.hpp"
#include "dl_detect_pico_postprocessor.hpp"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "pedestrian_pico";

namespace {

// DetectImpl is ESP-DL's "preprocess -> model -> postprocess" detector. Its three parts are
// protected members, so a small subclass is the way to fill them in.
class PicoDetector : public dl::detect::DetectImpl {
public:
    // Returns false (and leaves nothing half-built) if the weights aren't a usable model.
    bool load(const uint8_t *weights, float score_thr, float nms_thr)
    {
        // "RODATA" here just means "the model is at this memory address" (ours is in PSRAM).
        // param_copy = false: use the weights where they are instead of copying them again.
        m_model = new dl::Model((const char *)weights, fbs::MODEL_LOCATION_IN_FLASH_RODATA, 0,
                                dl::MEMORY_MANAGER_GREEDY, nullptr, false);
        if (!m_model->is_built()) {
            delete m_model;
            m_model = nullptr;
            return false;
        }
        m_model->minimize();   // free what is only needed for debugging the model
        // Preprocess: resize the frame to the model's 224x224 input; pixel values stay 0..255
        // (mean 0, std 1).
        m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1});
        // Postprocess: decode the three output scales (strides 8, 16, 32) into boxes, drop
        // boxes below score_thr, merge overlapping ones (NMS), keep the best 10.
        m_postprocessor = new dl::detect::PicoPostprocessor(
            m_model, m_image_preprocessor, score_thr, nms_thr, NS_INFER_MAX_ITEMS,
            {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});
        return true;
    }
    PicoDetector()
    {
        m_model = nullptr;
        m_image_preprocessor = nullptr;
        m_postprocessor = nullptr;
    }
};

PicoDetector *s_det = nullptr;
float s_score_thr = CONFIG_NS_PEDESTRIAN_PICO_SCORE_PCT / 100.0f;
const float s_nms_thr = CONFIG_NS_PEDESTRIAN_PICO_NMS_PCT / 100.0f;

esp_err_t model_init(const uint8_t *weights, size_t len)
{
    if (s_det) return ESP_OK;   // already loaded
    if (!weights || len < 16) return ESP_ERR_INVALID_ARG;
    PicoDetector *det = new (std::nothrow) PicoDetector();
    if (!det) return ESP_ERR_NO_MEM;
    if (!det->load(weights, s_score_thr, s_nms_thr)) {
        delete det;
        return ESP_FAIL;   // not an .espdl file, or damaged
    }
    s_det = det;
    ESP_LOGI(TAG, "score threshold %.2f, NMS threshold %.2f", s_score_thr, s_nms_thr);
    return ESP_OK;
}

esp_err_t model_run(const ns_frame_t *frame, ns_result_t *out)
{
    // Describe the pixels to ESP-DL. Nothing is copied; ESP-DL only reads them.
    dl::image::img_t img = {};
    img.data = (void *)frame->data;
    img.width = (uint16_t)frame->width;
    img.height = (uint16_t)frame->height;
    img.pix_type = frame->format == NS_PIX_RGB565_BE ? dl::image::DL_IMAGE_PIX_TYPE_RGB565BE
                                                     : dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

    std::list<dl::detect::result_t> &res = s_det->run(img);

    // Copy ESP-DL's C++ list into the plain C result (the next run() overwrites the list).
    out->count = 0;
    for (const auto &r : res) {
        if (out->count >= NS_INFER_MAX_ITEMS) break;
        ns_item_t *it = &out->item[out->count++];
        it->cls = 0;   // the model knows one class
        it->score = r.score;
        it->x1 = r.box[0];
        it->y1 = r.box[1];
        it->x2 = r.box[2];
        it->y2 = r.box[3];
    }
    return ESP_OK;
}

// "score_thr": boxes below this score are dropped by the model (0..1).
esp_err_t model_set_param(const char *key, float value)
{
    if (strcmp(key, "score_thr") != 0) return ESP_ERR_NOT_FOUND;
    s_score_thr = value;
    if (s_det) s_det->set_score_thr(value);
    return ESP_OK;
}

void model_deinit(void)
{
    delete s_det;
    s_det = nullptr;
}

const char *const s_labels[] = {"person"};

const ns_model_t pedestrian_pico = {
    .name = "pedestrian_pico",
    .task = NS_TASK_DETECT,
    .labels = s_labels,
    .n_labels = 1,
    .weights_file = "pedestrian_detect_pico_s8_v1.espdl",
    .init = model_init,
    .run = model_run,
    .set_param = model_set_param,
    .deinit = model_deinit,
};

}   // namespace

NS_MODEL_REGISTER(pedestrian_pico)
