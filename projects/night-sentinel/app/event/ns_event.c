#include "ns_event.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "event";

#define NVS_NAMESPACE "ns_event"
#define NVS_KEY "next_id"

static uint32_t s_next_id = 0;                 // 0 = not loaded from NVS yet
static SemaphoreHandle_t s_lock = NULL;

static uint32_t take_next_id(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t h;
    bool have_nvs = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK;
    if (s_next_id == 0) {
        s_next_id = 1;
        if (have_nvs) {
            nvs_get_u32(h, NVS_KEY, &s_next_id);   // keeps 1 if the key doesn't exist yet
        }
    }
    uint32_t id = s_next_id++;
    if (have_nvs) {
        nvs_set_u32(h, NVS_KEY, s_next_id);
        nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreGive(s_lock);
    return id;
}

uint32_t ns_event_count(void)
{
    if (s_next_id == 0) {   // nothing fired since boot: read the stored counter
        nvs_handle_t h;
        uint32_t next = 1;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
            nvs_get_u32(h, NVS_KEY, &next);
            nvs_close(h);
        }
        return next - 1;
    }
    return s_next_id - 1;
}

ns_event_t *ns_event_new(uint8_t *jpg, size_t jpg_len)
{
    ns_event_t *ev = calloc(1, sizeof(*ev));   // calloc = malloc + set every byte to 0
    if (!ev) {
        free(jpg);
        ESP_LOGE(TAG, "out of memory");
        return NULL;
    }
    ev->id = take_next_id();
    ev->uptime_ms = esp_timer_get_time() / 1000;
    time_t now = time(NULL);
    ev->wall = now > 1700000000 ? now : 0;   // before SNTP sync the clock starts at 1970
    ev->lux = -1;
    ev->jpg = jpg;
    ev->jpg_len = jpg_len;
    ev->push_result = PUSH_PENDING;
    atomic_init(&ev->refs, 1);
    return ev;
}

void ns_event_retain(ns_event_t *ev)
{
    atomic_fetch_add(&ev->refs, 1);
}

void ns_event_release(ns_event_t *ev)
{
    // atomic_fetch_sub returns the value *before* subtracting: 1 means we were the last owner.
    if (atomic_fetch_sub(&ev->refs, 1) == 1) {
        free(ev->jpg);
        free(ev);
    }
}

void ns_event_name(const ns_event_t *ev, char *buf, size_t len)
{
    snprintf(buf, len, "E%05lu", (unsigned long)ev->id);
}
