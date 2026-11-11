// SD evidence for one event: a JPEG in ev/ and a row in events.csv.
// Runs as a job on the platform's storage task, so card writes never stall detection.
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sentinel.h"
#include "storage.h"

static const char *TAG = "evidence";

#define PUSH_WAIT_MS 25000
#define EVENTS_HEADER "event_id,time,uptime_s,score,x1,y1,x2,y2,ir,lux,test,jpeg_file,jpeg_bytes,jpeg_ok,write_ms,push_ok,push_ms"

static void save_job(void *arg)
{
    ns_event_t *ev = arg;
    char name[12], file[64] = "";
    ns_event_name(ev, name, sizeof(name));
    if (ev->wall) {
        struct tm tm;
        localtime_r(&ev->wall, &tm);
        char stamp[20];
        strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
        snprintf(file, sizeof(file), "ev/%s_%s.jpg", name, stamp);
    } else {
        snprintf(file, sizeof(file), "ev/%s_up%lld.jpg", name, ev->uptime_ms / 1000);
    }

    int write_ms = -1;
    bool jpeg_ok = ev->jpg && ns_storage_write_file(file, ev->jpg, ev->jpg_len, &write_ms);

    // Wait for the phone push to finish, so the row can record it.
    int64_t t_wait = esp_timer_get_time();
    while (ev->push_result == PUSH_PENDING && esp_timer_get_time() - t_wait < PUSH_WAIT_MS * 1000LL) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    char when[40], line[320];
    if (ev->wall) {
        struct tm tm;
        localtime_r(&ev->wall, &tm);
        char tz[8];
        strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%S", &tm);
        strftime(tz, sizeof(tz), "%z", &tm);
        size_t n = strlen(when);
        snprintf(when + n, sizeof(when) - n, "%.3s:%.2s", tz, tz + 3);
    } else {
        when[0] = '\0';
    }
    snprintf(line, sizeof(line), "%s,%s,%.1f,%.2f,%d,%d,%d,%d,%d,%.1f,%d,%s,%u,%d,%d,%d,%d",
             name, when, ev->uptime_ms / 1000.0, ev->score, ev->box[0], ev->box[1], ev->box[2],
             ev->box[3], ev->ir, ev->lux, ev->test, jpeg_ok ? file : "", (unsigned)ev->jpg_len,
             jpeg_ok, write_ms, ev->push_result == PUSH_OK, ev->push_ms);
    bool row_ok = ns_storage_mounted() && ns_storage_append_now("events.csv", EVENTS_HEADER, line);
    ESP_LOGI(TAG, "%s: jpeg %s (%d ms), csv row %s", name, jpeg_ok ? "saved" : "NOT saved",
             write_ms, row_ok ? "saved" : "NOT saved");
    ns_event_release(ev);
}

void sentinel_save_event(ns_event_t *ev)
{
    ns_event_retain(ev);
    if (!ns_storage_run(save_job, ev)) {
        ESP_LOGW(TAG, "storage queue full, event not saved");
        ns_event_release(ev);
    }
}
