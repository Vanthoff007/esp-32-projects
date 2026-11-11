#include "frame_hub.h"

#include <stdlib.h>
#include <string.h>

#include "camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "sdkconfig.h"

static const char *TAG = "hub";

#define DEMAND_MS 200    // keep encoding this long after the last ns_hub_get_jpeg() call

static ns_frame_sink_t s_sink;                 // raw-frame user (wanted == NULL: none)
static volatile int64_t s_demand_until_us = 0; // JPEGs are wanted until this time
static volatile uint32_t s_null_frames = 0;

// Hand-over from the capture task to the encoder task: one raw frame copy.
static uint8_t *s_raw = NULL;
static size_t s_raw_cap = 0, s_raw_len = 0;
static int s_raw_w, s_raw_h;
static int64_t s_raw_t_us;
static atomic_bool s_raw_busy = false;         // true from the copy until the JPEG is done
static TaskHandle_t s_encoder = NULL;

// The newest JPEG. The spinlock only guards the pointer swap and the reference count bump.
static ns_jpeg_t *s_latest = NULL;
static volatile uint32_t s_seq = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void ns_hub_set_sink(const ns_frame_sink_t *sink)
{
    if (sink) s_sink = *sink;
    else memset(&s_sink, 0, sizeof(s_sink));
}

uint32_t ns_hub_seq(void)
{
    return s_seq;
}

uint32_t ns_hub_null_frames(void)
{
    return s_null_frames;
}

void ns_jpeg_release(ns_jpeg_t *jpg)
{
    // atomic_fetch_sub returns the value before subtracting: 1 means we were the last user.
    if (jpg && atomic_fetch_sub(&jpg->refs, 1) == 1) {
        free(jpg->buf);
        free(jpg);
    }
}

ns_jpeg_t *ns_hub_get_jpeg(uint32_t after_seq, int timeout_ms)
{
    int64_t deadline = esp_timer_get_time() + timeout_ms * 1000LL;
    while (true) {
        s_demand_until_us = esp_timer_get_time() + DEMAND_MS * 1000LL;
        ns_jpeg_t *j = NULL;
        portENTER_CRITICAL(&s_mux);
        if (s_latest && s_latest->seq > after_seq) {
            j = s_latest;
            atomic_fetch_add(&j->refs, 1);
        }
        portEXIT_CRITICAL(&s_mux);
        if (j) return j;
        if (esp_timer_get_time() >= deadline) return NULL;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static bool jpeg_wanted(void)
{
    return esp_timer_get_time() < s_demand_until_us;
}

// Capture task side: copy the frame for the encoder, unless it is still busy with the last one.
static void offer_to_encoder(const camera_fb_t *fb)
{
    if (fb->format != PIXFORMAT_RGB565 || atomic_exchange(&s_raw_busy, true)) {
        return;
    }
    if (s_raw_cap < fb->len) {
        heap_caps_free(s_raw);
        s_raw = heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM);
        s_raw_cap = s_raw ? fb->len : 0;
    }
    if (!s_raw) {
        atomic_store(&s_raw_busy, false);
        return;
    }
    memcpy(s_raw, fb->buf, fb->len);
    s_raw_len = fb->len;
    s_raw_w = fb->width;
    s_raw_h = fb->height;
    s_raw_t_us = esp_timer_get_time();
    xTaskNotifyGive(s_encoder);
}

static void encoder_task(void *arg)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // sleep until the capture task hands over a frame
        uint8_t *buf = NULL;
        size_t len = 0;
        bool ok = fmt2jpg(s_raw, s_raw_len, s_raw_w, s_raw_h, PIXFORMAT_RGB565,
                          CONFIG_NS_JPEG_QUALITY, &buf, &len);
        if (ok) {
            // The encoder always hands back a 128 KB buffer; a QVGA JPEG is ~9 KB. Give the rest back.
            uint8_t *fit = heap_caps_realloc(buf, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (fit) buf = fit;
        }
        ns_jpeg_t *j = ok ? calloc(1, sizeof(*j)) : NULL;
        if (j) {
            j->buf = buf;
            j->len = len;
            j->t_us = s_raw_t_us;
            j->width = s_raw_w;
            j->height = s_raw_h;
            atomic_init(&j->refs, 1);   // the hub's own reference
            portENTER_CRITICAL(&s_mux);
            j->seq = ++s_seq;
            ns_jpeg_t *old = s_latest;
            s_latest = j;
            portEXIT_CRITICAL(&s_mux);
            ns_jpeg_release(old);
        } else {
            free(buf);
        }
        atomic_store(&s_raw_busy, false);
    }
}

static void capture_task(void *arg)
{
    esp_task_wdt_add(NULL);   // reset the chip if this loop ever hangs (timeout: sdkconfig)
    while (true) {
        esp_task_wdt_reset();
        bool want_raw = s_sink.wanted && s_sink.wanted(s_sink.ctx);
        bool want_jpeg = jpeg_wanted();
        if (!want_raw && !want_jpeg) {
            vTaskDelay(pdMS_TO_TICKS(100));   // nobody is looking: leave the camera alone
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        camera_fb_t *fb = ns_cam_grab();
        int64_t grab_us = esp_timer_get_time() - t0;
        if (!fb) {
            s_null_frames++;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (want_jpeg) offer_to_encoder(fb);
        if (want_raw) s_sink.on_frame(fb, grab_us, s_sink.ctx);
        ns_cam_release(fb);
        if (!want_raw) vTaskDelay(1);   // JPEG only: nothing slow in this loop, so make room for others
    }
}

esp_err_t ns_hub_start(void)
{
    if (s_encoder) return ESP_OK;
    // The encoder's stack lives in PSRAM: internal RAM is the scarce one on this board.
    if (xTaskCreatePinnedToCoreWithCaps(encoder_task, "jpeg", 6144, NULL, 2, &s_encoder, 0,
                                        MALLOC_CAP_SPIRAM) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    // 8 KB: the model runs on this task's stack.
    if (xTaskCreatePinnedToCore(capture_task, "capture", 8192, NULL, 5, NULL, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "frame hub started");
    return ESP_OK;
}
