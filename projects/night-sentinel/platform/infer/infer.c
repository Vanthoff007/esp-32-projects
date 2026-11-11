// The inference engine: runs the registered model on camera frames and hands the results on.
// It knows nothing about any particular model; see include/infer.h for the contract.
#include "infer.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "debug_http.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "frame_hub.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static const char *TAG = "infer";

#define TIMING_EVERY_FRAMES 50    // one stage-timing summary line per 50 frames
#define LOAD_RETRY_MS 10000       // while the model can't be loaded, try again this often

static const ns_model_t *s_model = NULL;
static SemaphoreHandle_t s_lock = NULL;      // one run() at a time (models aren't thread-safe)
static ns_infer_listener_t s_listener;

#if CONFIG_NS_INFER_AT_BOOT
static volatile bool s_enabled = true;
#else
static volatile bool s_enabled = false;
#endif
static volatile bool s_ready = false;        // init() succeeded
static volatile bool s_running = false;      // enabled and ready: frames are being processed
static int64_t s_next_load_us = 0;
static char s_msg[224] = "not started";
static uint8_t *s_weights = NULL;        // the weights file, in PSRAM
static size_t s_weights_len = 0;

static volatile float s_fps = 0, s_infer_ms = 0;
static volatile uint32_t s_frames = 0;

// Timing window, only touched by the capture task.
static int64_t s_sum_grab, s_sum_infer, s_sum_other, s_win_start;
static int s_win_frames;

// ---- registry --------------------------------------------------------------------------

void ns_infer_register(const ns_model_t *model)
{
    // Runs before app_main(), so no logging here. One model per build: the first one wins.
    if (!s_model) s_model = model;
}

const ns_model_t *ns_infer_model(void)
{
    return s_model;
}

int ns_infer_label_index(const char *label)
{
    if (!s_model) return -1;
    for (int i = 0; i < s_model->n_labels; i++) {
        if (strcmp(s_model->labels[i], label) == 0) return i;
    }
    return -1;
}

// ---- switches and state ----------------------------------------------------------------

void ns_infer_enable(bool on)
{
    s_enabled = on;
    if (on) s_next_load_us = 0;   // pressing Start retries a failed load at once
}

bool ns_infer_enabled(void)
{
    return s_enabled;
}

bool ns_infer_ready(void)
{
    return s_ready;
}

const char *ns_infer_message(void)
{
    return s_msg;
}

void ns_infer_set_listener(const ns_infer_listener_t *listener)
{
    if (listener) s_listener = *listener;
    else memset(&s_listener, 0, sizeof(s_listener));
}

void ns_infer_get_stats(ns_infer_stats_t *stats)
{
    stats->fps = s_fps;
    stats->infer_ms = s_infer_ms;
    stats->frames = s_frames;
}

esp_err_t ns_infer_set_param(const char *key, float value)
{
    if (!s_model || !s_model->set_param) return ESP_ERR_NOT_SUPPORTED;
    if (!s_lock) return s_model->set_param(key, value);   // before ns_infer_start(): nothing runs yet
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = s_model->set_param(key, value);
    xSemaphoreGive(s_lock);
    return err;
}

// ---- loading and running ---------------------------------------------------------------

// Read the model's weights file from the SD card into PSRAM, 4 KB at a time (small reads keep
// the card driver's temporary buffers, which come out of the scarce internal RAM, small).
static bool read_weights(void)
{
    char path[160];
    snprintf(path, sizeof(path), "%s/%s", CONFIG_NS_MODEL_DIR, s_model->weights_file);
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0) {
        snprintf(s_msg, sizeof(s_msg), "weights file %s not found on the SD card", path);
        ESP_LOGW(TAG, "%s (copy it with: uv run sd.py push-model %s)", s_msg, s_model->name);
        return false;
    }
    FILE *f = fopen(path, "rb");
    uint8_t *buf = f ? heap_caps_aligned_alloc(16, st.st_size, MALLOC_CAP_SPIRAM) : NULL;
    size_t got = 0;
    while (buf && got < (size_t)st.st_size) {
        size_t want = (size_t)st.st_size - got < 4096 ? (size_t)st.st_size - got : 4096;
        size_t n = fread(buf + got, 1, want, f);
        if (n == 0) break;
        got += n;
    }
    if (f) fclose(f);
    if (!buf || got != (size_t)st.st_size) {
        heap_caps_free(buf);
        snprintf(s_msg, sizeof(s_msg), "could not read %s", path);
        ESP_LOGE(TAG, "%s", s_msg);
        return false;
    }
    s_weights = buf;
    s_weights_len = got;
    return true;
}

static void try_load(void)
{
    int64_t t0 = esp_timer_get_time();
    if (s_model->weights_file && !s_weights && !read_weights()) return;
    int64_t t1 = esp_timer_get_time();
    unsigned int_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    // Building a model makes thousands of small allocations. Normally anything under 4 KB
    // comes out of internal RAM, and with Wi-Fi running there isn't enough of it: the board
    // ran out and reset (issues log, 2026-11-11). So while the model loads, send small
    // allocations to the 8 MB PSRAM too, then put the normal rule back.
    heap_caps_malloc_extmem_enable(32);
    esp_err_t err = s_model->init(s_weights, s_weights_len);
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        snprintf(s_msg, sizeof(s_msg), "model failed to load: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "%s: %s", s_model->name, s_msg);
        heap_caps_free(s_weights);   // maybe a bad file: read it again next time
        s_weights = NULL;
        s_weights_len = 0;
        return;
    }
    ESP_LOGI(TAG, "weights: %u KB read from the card in %lld ms; internal RAM %u -> %u KB free (lowest ever %u KB)",
             (unsigned)(s_weights_len / 1024), (t1 - t0) / 1000, int_before / 1024,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
    s_ready = true;
    snprintf(s_msg, sizeof(s_msg), "loaded");
    ESP_LOGI(TAG, "model %s loaded in %lld ms (%.1f s after boot)", s_model->name,
             (esp_timer_get_time() - t0) / 1000, esp_timer_get_time() / 1e6);
}

esp_err_t ns_infer_run(const ns_frame_t *frame, ns_result_t *out)
{
    if (!frame || !frame->data || !out || frame->width <= 0 || frame->height <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out->count = 0;
    out->time_us = 0;
    out->t_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (!s_model || !s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = s_model->run(frame, out);
    out->time_us = esp_timer_get_time() - t0;
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) out->count = 0;
    return err;
}

esp_err_t ns_infer_run_jpeg(const uint8_t *jpg, size_t len, ns_result_t *out, int *w, int *h)
{
    if (!jpg || len == 0 || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    // Decode into the camera's own format, so this tests exactly what live frames go through.
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;
    jpeg_dec_handle_t dec = NULL;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) {
        return ESP_ERR_NO_MEM;
    }
    jpeg_dec_io_t io = {.inbuf = (uint8_t *)jpg, .inbuf_len = (int)len};
    jpeg_dec_header_info_t info = {0};
    esp_err_t err = ESP_FAIL;   // not a JPEG, or a kind the decoder can't read
    uint8_t *px = NULL;
    if (jpeg_dec_parse_header(dec, &io, &info) == JPEG_ERR_OK) {
        size_t px_len = (size_t)info.width * info.height * 2;
        px = jpeg_calloc_align(px_len, 16);   // the decoder needs a 16-byte aligned buffer
        io.outbuf = px;
        if (!px) {
            err = ESP_ERR_NO_MEM;
        } else if (jpeg_dec_process(dec, &io) == JPEG_ERR_OK) {
            if (w) *w = info.width;
            if (h) *h = info.height;
            ns_frame_t frame = {.data = px, .len = px_len, .width = info.width,
                                .height = info.height, .format = NS_PIX_RGB565_BE};
            err = ns_infer_run(&frame, out);
        }
    }
    jpeg_dec_close(dec);
    if (px) jpeg_free_align(px);
    return err;
}

// ---- the live loop: called by the frame hub's capture task ----------------------------------

static void set_running(bool run)
{
    if (run == s_running) return;
    s_running = run;
    ESP_LOGW(TAG, "inference %s", run ? "started" : "stopped");
    s_sum_grab = s_sum_infer = s_sum_other = 0;   // start a fresh timing window
    s_win_frames = 0;
    s_win_start = esp_timer_get_time();
    if (!run) s_fps = 0;
    if (s_listener.on_running) s_listener.on_running(run, s_listener.ctx);
}

// Does the engine want the next camera frame?
static bool frame_wanted(void *ctx)
{
    bool run = s_enabled && s_model;
    if (run && !s_ready) {
        if (esp_timer_get_time() >= s_next_load_us) {
            s_next_load_us = esp_timer_get_time() + LOAD_RETRY_MS * 1000LL;
            try_load();
        }
        run = s_ready;
    }
    set_running(run);
    return run;
}

static void print_frame_csv(const ns_result_t *res)
{
#if CONFIG_NS_FRAME_CSV
    char line[48 + NS_INFER_MAX_ITEMS * 32];
    int n = snprintf(line, sizeof(line), "F,%" PRIu32 ",%d", res->t_ms, res->count);
    for (int i = 0; i < res->count && n < (int)sizeof(line) - 32; i++) {
        const ns_item_t *b = &res->item[i];
        n += snprintf(line + n, sizeof(line) - n, ",%.2f,%d,%d,%d,%d", b->score, b->x1, b->y1, b->x2, b->y2);
    }
    puts(line);   // plain line without a log prefix, so tools can split it easily
#endif
}

static void on_frame(const camera_fb_t *fb, int64_t grab_us, void *ctx)
{
    int64_t t0 = esp_timer_get_time();
    ns_frame_t frame = {.data = fb->buf, .len = fb->len, .width = fb->width, .height = fb->height,
                        .format = NS_PIX_RGB565_BE};
    ns_result_t res;
    ns_infer_run(&frame, &res);   // on failure res.count is 0

    print_frame_csv(&res);
    if (s_listener.on_result) s_listener.on_result(&frame, &res, s_listener.ctx);
    int64_t t1 = esp_timer_get_time();

    s_frames++;
    s_sum_grab += grab_us;
    s_sum_infer += res.time_us;
    s_sum_other += (t1 - t0) - res.time_us;   // everything that isn't the grab or the model
    if (++s_win_frames == TIMING_EVERY_FRAMES) {
        float secs = (t1 - s_win_start) / 1e6f;
        s_fps = s_win_frames / secs;
        s_infer_ms = s_sum_infer / 1000.0f / s_win_frames;
        printf("T,frames=%d,grab_ms=%.1f,infer_ms=%.1f,other_ms=%.1f,loop_ms=%.1f,fps=%.2f\n",
               s_win_frames, s_sum_grab / 1000.0f / s_win_frames, s_infer_ms,
               s_sum_other / 1000.0f / s_win_frames, secs * 1000 / s_win_frames, s_fps);
        s_sum_grab = s_sum_infer = s_sum_other = 0;
        s_win_frames = 0;
        s_win_start = t1;
    }
}

// ---- web server hooks ------------------------------------------------------------------

#define DETECT_MAX_BODY (400 * 1024)

// POST /detect, body = one JPEG. Runs the model on it and answers with JSON:
//   {"model":"pedestrian_pico","w":320,"h":240,"ms":131,"boxes":[[0.78,112,40,190,236], ...],
//    "cls":[0, ...],"labels":["person"]}
// boxes = score, x1, y1, x2, y2 per item; cls = the label index of the same item;
// labels = the model's label names (cut short if a model has very many).
// tools/eval_detector.py sends the test images through this.
static esp_err_t detect_handler(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > DETECT_MAX_BODY) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "send a JPEG body (max 400 KB)");
        return ESP_FAIL;
    }
    uint8_t *buf = heap_caps_malloc(req->content_len, MALLOC_CAP_SPIRAM);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, (char *)buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            heap_caps_free(buf);
            return ESP_FAIL;
        }
        got += r;
    }
    ns_result_t res;
    int w = 0, h = 0;
    esp_err_t err = ns_infer_run_jpeg(buf, got, &res, &w, &h);
    heap_caps_free(buf);
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, s_model ? s_msg : "no model in this build");
    }
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "could not decode (colour baseline JPEG only)");
        return ESP_FAIL;
    }
    static char json[1500];   // static: one request at a time, keeps it off the stack
    int n = snprintf(json, sizeof(json), "{\"model\":\"%s\",\"w\":%d,\"h\":%d,\"ms\":%lld,\"boxes\":[",
                     s_model->name, w, h, res.time_us / 1000);
    for (int i = 0; i < res.count && n < (int)sizeof(json) - 700; i++) {
        const ns_item_t *b = &res.item[i];
        n += snprintf(json + n, sizeof(json) - n, "%s[%.3f,%d,%d,%d,%d]", i ? "," : "", b->score,
                      b->x1, b->y1, b->x2, b->y2);
    }
    n += snprintf(json + n, sizeof(json) - n, "],\"cls\":[");
    for (int i = 0; i < res.count && n < (int)sizeof(json) - 640; i++) {
        n += snprintf(json + n, sizeof(json) - n, "%s%d", i ? "," : "", res.item[i].cls);
    }
    n += snprintf(json + n, sizeof(json) - n, "],\"labels\":[");
    for (int i = 0; i < s_model->n_labels && n < (int)sizeof(json) - 48; i++) {
        n += snprintf(json + n, sizeof(json) - n, "%s\"%.32s\"", i ? "," : "", s_model->labels[i]);
    }
    snprintf(json + n, sizeof(json) - n, "]}");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

// /action?name=detect&arg=0|1 : stop / start inference.
static esp_err_t act_detect(int arg, char *msg, size_t len)
{
    if (!s_model) {
        snprintf(msg, len, "this firmware was built without a model (menuconfig -> Platform: inference)");
        return ESP_OK;
    }
    ns_infer_enable(arg != 0);
    snprintf(msg, len, "inference %s (model %s)", arg ? "ON" : "STOPPED", s_model->name);
    return ESP_OK;
}

static int status_fields(char *buf, size_t len)
{
    return snprintf(buf, len,
                    "\"detect\":%d,\"model\":\"%s\",\"model_ready\":%d,\"model_msg\":\"%s\","
                    "\"fps\":%.2f,\"infer_ms\":%.1f,\"frames\":%" PRIu32,
                    s_running, s_model ? s_model->name : "none", s_ready,
                    s_model ? s_msg : "no model in this build", s_fps, s_infer_ms, s_frames);
}

esp_err_t ns_infer_start(void)
{
    if (s_lock) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    ns_debug_http_add_route("/detect", HTTP_POST, detect_handler);
    ns_debug_http_add_action("detect", act_detect);
    ns_debug_http_add_status(status_fields);

    if (!s_model) {
        ESP_LOGW(TAG, "no model in this build: inference is off (menuconfig -> Platform: inference)");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "model %s (%s, %d label%s), inference %s at boot", s_model->name,
             s_model->task == NS_TASK_DETECT ? "detector" : "classifier", s_model->n_labels,
             s_model->n_labels == 1 ? "" : "s", s_enabled ? "on" : "off");
    // Load the model now, before Wi-Fi starts and memory gets tight. If the weights file isn't
    // on the card yet, the capture task tries again every 10 s while inference is switched on.
    try_load();
    s_next_load_us = esp_timer_get_time() + LOAD_RETRY_MS * 1000LL;
    static const ns_frame_sink_t sink = {.wanted = frame_wanted, .on_frame = on_frame};
    ns_hub_set_sink(&sink);
    return ESP_OK;
}
