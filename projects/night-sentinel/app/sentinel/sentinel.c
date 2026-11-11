// Night Sentinel app logic: model results -> intrusion state machine -> speaker + phone push
// + SD evidence + video clip. See include/sentinel.h.
//
// Tasks this file adds:
//   monitor  core 0, prio 2   day/night switch every 2 s, heartbeat every 60 s
// The result handling below runs on the platform's capture task (core 1).
#include "sentinel.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "debug_http.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "frame_hub.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "infer.h"
#include "intrusion_config.h"
#include "intrusion_fsm.h"
#include "ir.h"
#include "night.h"
#include "notify.h"
#include "ns_time.h"
#include "recorder.h"
#include "sdkconfig.h"
#include "status_led.h"
#include "storage.h"

#define EVENT_JPEG_QUALITY 80

static const char *TAG = "sentinel";

// The web page: www/index.html, built into the firmware (EMBED_TXTFILES in CMakeLists.txt).
// The linker provides its start and end; the build adds a closing NUL byte.
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

// ---------------------------------------------------------------------------------------------
// Shared state. Written by one task, read by others (status page, heartbeat). Single values
// of 32 bits or less are read/written in one go on this CPU, so no lock is needed for them.

static volatile fsm_state_t s_state = ST_IDLE;
static volatile float s_last_best = 0;      // best box score on the latest frame (any box)
static volatile uint32_t s_alerts = 0;      // alerts fired since boot (real + test)
// Accept score override in %, -1 = intrusion_config.h. Set from the web page; back to the
// default on every reboot.
static volatile int s_accept_pct = -1;
// Best box of the latest frame, for the box drawn over the live view: x1,y1,x2,y2,accepted.
static volatile int s_last_box[5] = {0};

// Only touched by the capture task (on_result / on_running).
static intrusion_fsm_t s_fsm;
static fsm_params_t s_params;
static float s_default_accept;
static int s_person_cls = -1;               // index of the "person" label in the model

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---------------------------------------------------------------------------------------------
// Events: one JPEG, then fan out to speaker, phone, SD card and the video recorder.

static void push_done(void *ctx, ns_push_status_t status, int ms)
{
    ns_event_t *ev = ctx;
    ev->push_ms = ms;
    ev->push_result = status == NS_PUSH_OK ? PUSH_OK : status == NS_PUSH_FAILED ? PUSH_FAILED : PUSH_SKIPPED;
    ns_event_release(ev);   // the reference taken in push_event()
}

static void push_event(ns_event_t *ev)
{
    char name[12], when[40], msg[160], file[24];
    ns_event_name(ev, name, sizeof(name));
    ns_time_iso(ev->wall, when, sizeof(when));
    snprintf(msg, sizeof(msg), "%s %s score %.2f, IR %s%s", name, when, ev->score,
             ev->ir ? "on" : "off", ev->test ? " (test)" : "");
    snprintf(file, sizeof(file), "%s.jpg", name);
    ns_event_retain(ev);    // the JPEG must stay alive until push_done()
    ns_notify_push("Intruder detected", msg, file, ev->jpg, ev->jpg_len, push_done, ev);
}

// Takes ownership of jpg. Called from the capture task (real) or the web server (tests).
static void fire_event(uint8_t *jpg, size_t jpg_len, float score, const int box[4], bool test,
                       bool speak)
{
    ns_event_t *ev = ns_event_new(jpg, jpg_len);
    if (!ev) return;
    ev->score = score;
    memcpy(ev->box, box, sizeof(ev->box));
    ev->ir = ir_get();
    ev->lux = ns_night_lux();
    ev->test = test;
    s_alerts++;

    char name[12];
    ns_event_name(ev, name, sizeof(name));
    bool spoke = speak && ns_audio_play(CLIP_WARN);
    ESP_LOGW(TAG, "INTRUSION %s%s: score %.2f box %d,%d-%d,%d, jpeg %u bytes, speaker %s",
             name, test ? " (test)" : "", score, box[0], box[1], box[2], box[3],
             (unsigned)jpg_len, spoke ? "on" : "busy/off");
    push_event(ev);              // each of these takes its own reference ...
    sentinel_save_event(ev);
    ns_rec_trigger(name);        // a video clip, if recording is set to "event clips"
    ns_event_release(ev);        // ... so we can drop ours now
}

// ---------------------------------------------------------------------------------------------
// Model results (capture task, core 1).

static void on_running(bool running, void *ctx)
{
    printf("S,%" PRIu32 ",%s,%s,%" PRIu32 "\n", now_ms(), running ? "PAUSED" : fsm_state_name(fsm_state(&s_fsm)),
           running ? fsm_state_name(ST_IDLE) : "PAUSED", ns_event_count());
    // Stopped or started: forget any half-confirmed person.
    fsm_init(&s_fsm, &s_params);
    s_state = ST_IDLE;
    s_last_best = 0;
    for (int i = 0; i < 5; i++) s_last_box[i] = 0;
    ns_led_set_mode(NS_LED_IDLE);
}

static void on_result(const ns_frame_t *frame, const ns_result_t *res, void *ctx)
{
    s_params.accept_score = s_accept_pct >= 0 ? s_accept_pct / 100.0f : s_default_accept;

    // Is there a person? The best "person" box that passes the score / size filters.
    bool hit = false;
    float best = 0, best_any = 0;
    int best_box[4] = {0};
    int any_box[4] = {0};
    for (int i = 0; i < res->count; i++) {
        const ns_item_t *b = &res->item[i];
        if (b->cls != s_person_cls) continue;
        if (b->score > best_any) {
            best_any = b->score;
            any_box[0] = b->x1, any_box[1] = b->y1, any_box[2] = b->x2, any_box[3] = b->y2;
        }
        if (fsm_box_accepted(&s_params, b->score, b->x1, b->y1, b->x2, b->y2, frame->width, frame->height) &&
            b->score > best) {
            hit = true;
            best = b->score;
            best_box[0] = b->x1, best_box[1] = b->y1, best_box[2] = b->x2, best_box[3] = b->y2;
        }
    }
    s_last_best = best_any;
    // For the page overlay: the accepted box if there is one, else the best raw box.
    const int *show = hit ? best_box : any_box;
    for (int i = 0; i < 4; i++) s_last_box[i] = show[i];
    s_last_box[4] = hit;

    fsm_state_t before = fsm_state(&s_fsm);
    fsm_action_t act = fsm_step(&s_fsm, (fsm_input_t){.hit = hit, .score = best, .now_ms = res->t_ms});
    fsm_state_t after = fsm_state(&s_fsm);

    if (act & ACT_ALERT) {
        // Encode the frame that confirmed the event, before the camera reuses its buffer.
        uint8_t *jpg = NULL;
        size_t jpg_len = 0;
        if (!fmt2jpg((uint8_t *)frame->data, frame->len, frame->width, frame->height, PIXFORMAT_RGB565,
                     EVENT_JPEG_QUALITY, &jpg, &jpg_len)) {
            jpg = NULL;
            jpg_len = 0;
        }
        fire_event(jpg, jpg_len, best, best_box, false, true);
    }
    if (after != before) {
        printf("S,%" PRIu32 ",%s,%s,%" PRIu32 "\n", res->t_ms, fsm_state_name(before), fsm_state_name(after),
               ns_event_count());
        s_state = after;
        ns_led_set_mode((after == ST_CONFIRMED || after == ST_ALERT_ACTIVE) ? NS_LED_ACTIVE : NS_LED_IDLE);
    }
    if (act & ACT_LOG_REAPPEAR) ESP_LOGI(TAG, "person seen again during cooldown (no new alert)");
    if (act & ACT_SUPPRESSED_BY_CAP) ESP_LOGW(TAG, "alert suppressed: %d alerts in the last hour", s_params.max_alerts_per_hour);
}

// ---------------------------------------------------------------------------------------------
// Monitor task: day/night switch, model health on the LED, heartbeat.

#define HEARTBEAT_HEADER "uptime_s,fps,infer_ms,free_int,min_int,free_psram,min_psram,rssi,events,state,ir,lux,sd_free_mb,null_frames"

static void heartbeat(void)
{
    int rssi = 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;
    uint32_t sd_total, sd_free;
    ns_storage_space(&sd_total, &sd_free);
    ns_infer_stats_t st;
    ns_infer_get_stats(&st);
    char line[200];
    snprintf(line, sizeof(line), "%lld,%.2f,%.1f,%u,%u,%u,%u,%d,%" PRIu32 ",%s,%d,%.1f,%" PRIu32 ",%" PRIu32,
             esp_timer_get_time() / 1000000, st.fps, st.infer_ms,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM), rssi, s_alerts,
             fsm_state_name(s_state), ir_get(), ns_night_lux(), sd_free, ns_hub_null_frames());
    printf("H,%" PRIu32 ",%s\n", now_ms(), line);
    ns_storage_append("heartbeat.csv", HEARTBEAT_HEADER, line);
    if (sd_total && sd_free * 10 < sd_total) {
        ESP_LOGW(TAG, "SD card almost full: %" PRIu32 " of %" PRIu32 " MB free", sd_free, sd_total);
    }
}

static void monitor_task(void *arg)
{
    int64_t last_hb = esp_timer_get_time();
    bool fault = false;
    while (true) {
        ns_night_poll();
        // The alarm can't work without its model: show that on the LED (fast blink).
        // (Two checks in a row, so the second or two a normal model load takes doesn't count.)
        static bool was_missing = false;
        bool missing = ns_infer_enabled() && !ns_infer_ready();
        bool now_fault = missing && was_missing;
        was_missing = missing;
        if (now_fault != fault) {
            fault = now_fault;
            ns_led_set_mode(fault ? NS_LED_ERROR : NS_LED_IDLE);
            if (fault) ESP_LOGE(TAG, "NOT MONITORING: %s", ns_infer_message());
        }
        if (CONFIG_NS_HEARTBEAT_S > 0 && esp_timer_get_time() - last_hb >= CONFIG_NS_HEARTBEAT_S * 1000000LL) {
            last_hb = esp_timer_get_time();
            heartbeat();
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ---------------------------------------------------------------------------------------------
// Web server hooks: extra /status fields and /action test commands.

static int status_fields(char *buf, size_t len)
{
    fsm_params_t p = fsm_default_params();
    float accept = s_accept_pct >= 0 ? s_accept_pct / 100.0f : p.accept_score;
    return snprintf(buf, len,
                    "\"accept\":%.2f,\"box\":[%d,%d,%d,%d],\"box_ok\":%d,\"fw\":\"%s\",\"params\":\"%s\","
                    "\"state\":\"%s\",\"best_score\":%.2f,\"alerts\":%" PRIu32 ",\"event_count\":%" PRIu32 ","
                    "\"push\":%d,\"time_synced\":%d,\"audio_gain\":%d",
                    accept, s_last_box[0], s_last_box[1], s_last_box[2], s_last_box[3], s_last_box[4],
                    NS_FW_VERSION, NS_PARAMS_VERSION, fsm_state_name(s_state), s_last_best, s_alerts,
                    ns_event_count(), ns_notify_configured(), ns_time_synced(), ns_audio_get_gain());
}

// A 320x240 colour test pattern as JPEG: lets the push/SD path be tested without sending a
// picture of the room anywhere.
static bool pattern_jpeg(uint8_t **jpg, size_t *len)
{
    const int w = 320, h = 240;
    uint8_t *px = heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
    if (!px) return false;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int r = x * 31 / w, g = y * 63 / h, b = ((x / 40 + y / 40) & 1) ? 31 : 0;
            uint16_t v = (r << 11) | (g << 5) | b;
            px[(y * w + x) * 2] = v >> 8;   // big-endian, like the camera
            px[(y * w + x) * 2 + 1] = v & 0xff;
        }
    }
    bool ok = fmt2jpg(px, w * h * 2, w, h, PIXFORMAT_RGB565, EVENT_JPEG_QUALITY, jpg, len);
    heap_caps_free(px);
    return ok;
}

// A fresh camera frame as a JPEG the caller owns (a copy of the frame hub's).
static bool camera_jpeg(uint8_t **jpg, size_t *len)
{
    ns_jpeg_t *j = ns_hub_get_jpeg(ns_hub_seq(), 3000);
    if (!j) return false;
    *jpg = malloc(j->len);
    if (*jpg) {
        memcpy(*jpg, j->buf, j->len);
        *len = j->len;
    }
    ns_jpeg_release(j);
    return *jpg != NULL;
}

// /action?name=alert_test&arg=0|1 : a full fake event (speaker + push + SD).
// arg=0 attaches the test pattern (default), arg=1 a real camera frame.
static esp_err_t act_alert_test(int arg, char *msg, size_t len)
{
    uint8_t *jpg = NULL;
    size_t jpg_len = 0;
    bool ok = arg == 1 ? camera_jpeg(&jpg, &jpg_len) : pattern_jpeg(&jpg, &jpg_len);
    if (!ok) return ESP_FAIL;
    int box[4] = {0, 0, 0, 0};
    uint32_t id = ns_event_count() + 1;
    fire_event(jpg, jpg_len, 0, box, true, true);
    snprintf(msg, len, "test event E%05" PRIu32 " fired (%s image, %u bytes): speaker, push, SD",
             id, arg == 1 ? "camera" : "test-pattern", (unsigned)jpg_len);
    return ESP_OK;
}

// /action?name=sd_test&arg=N : N fake events straight to the SD card (100 writes test).
static esp_err_t act_sd_test(int arg, char *msg, size_t len)
{
    int n = arg > 0 ? arg : 10;
    if (!ns_storage_mounted()) {
        snprintf(msg, len, "no SD card mounted");
        return ESP_FAIL;
    }
    uint8_t *pattern = NULL;
    size_t pattern_len = 0;
    if (!pattern_jpeg(&pattern, &pattern_len)) return ESP_FAIL;
    for (int i = 0; i < n; i++) {
        uint8_t *copy = malloc(pattern_len);
        if (!copy) break;
        memcpy(copy, pattern, pattern_len);
        ns_event_t *ev = ns_event_new(copy, pattern_len);
        if (!ev) break;
        ev->test = true;
        ev->push_result = PUSH_SKIPPED;   // don't wait for a push that won't happen
        sentinel_save_event(ev);
        ns_event_release(ev);
        vTaskDelay(pdMS_TO_TICKS(150));   // let the storage queue drain
    }
    free(pattern);
    vTaskDelay(pdMS_TO_TICKS(1000));
    int last_ms, median_ms;
    uint32_t writes, failures;
    ns_storage_write_stats(&last_ms, &median_ms, &writes, &failures);
    snprintf(msg, len, "queued %d events; card writes so far %" PRIu32 ", failures %" PRIu32 ", median %d ms, last %d ms",
             n, writes, failures, median_ms, last_ms);
    return ESP_OK;
}

// /action?name=push_test : text-only push.
static esp_err_t act_push_test(int arg, char *msg, size_t len)
{
    char text[96];
    snprintf(text, sizeof(text), "Test from Night Sentinel %s, uptime %lld s", NS_FW_VERSION,
             esp_timer_get_time() / 1000000);
    int64_t t0 = esp_timer_get_time();
    int status = ns_notify_text("Night Sentinel test", text);
    snprintf(msg, len, "HTTP %d in %lld ms%s", status, (esp_timer_get_time() - t0) / 1000,
             ns_notify_configured() ? "" : " (no topic set)");
    return status == 200 ? ESP_OK : ESP_FAIL;
}

static esp_err_t act_beep(int arg, char *msg, size_t len)
{
    bool ok = ns_audio_play(arg == 1 ? CLIP_WARN : CLIP_BEEP);
    snprintf(msg, len, ok ? "playing" : "busy or audio disabled");
    return ESP_OK;
}

static esp_err_t act_gain(int arg, char *msg, size_t len)
{
    ns_audio_set_gain(arg);
    snprintf(msg, len, "speaker gain %d %%", ns_audio_get_gain());
    return ESP_OK;
}

// /action?name=accept&arg=30..95 : accept score in %, for live tuning. arg=0 = back to the
// value in intrusion_config.h. Not saved: a reboot restores the default.
static esp_err_t act_accept(int arg, char *msg, size_t len)
{
    if (arg != 0 && (arg < 30 || arg > 95)) return ESP_ERR_INVALID_ARG;
    s_accept_pct = arg == 0 ? -1 : arg;
    snprintf(msg, len, "accept score %s", arg == 0 ? "back to default" : "set (until reboot)");
    return ESP_OK;
}

// ---------------------------------------------------------------------------------------------

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INTERRUPT WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK WATCHDOG";
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_BROWNOUT: return "BROWN-OUT";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_USB: return "USB";
    default: return "other";
    }
}

esp_err_t sentinel_start(void)
{
    s_params = fsm_default_params();
#if CONFIG_NS_DEMO_MODE
    s_params.cooldown_ms = NS_DEMO_COOLDOWN_MS;
#endif
    s_default_accept = s_params.accept_score;
    fsm_init(&s_fsm, &s_params);

    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "Night Sentinel %s, reset reason: %s", NS_FW_VERSION, reset_reason_name(reason));
    ESP_LOGI(TAG, "params %s: model thr %.2f, accept %.2f, min box h %.2f, max area %.2f, confirm %d/%d, "
                  "absent %" PRIu32 " ms, cooldown %" PRIu32 " ms, cap %d/h",
             NS_PARAMS_VERSION, NS_MODEL_SCORE_THR, s_params.accept_score, s_params.min_box_h,
             s_params.max_box_area, s_params.confirm_k, s_params.confirm_n, s_params.absent_ms,
             s_params.cooldown_ms, s_params.max_alerts_per_hour);

    // The model: any one with a "person" label will do.
    const ns_model_t *model = ns_infer_model();
    s_person_cls = ns_infer_label_index(CONFIG_NS_PERSON_LABEL);
    if (!model) {
        ESP_LOGE(TAG, "this build has no model: nothing will be detected");
    } else if (s_person_cls < 0) {
        ESP_LOGE(TAG, "model %s has no \"%s\" label: nothing will be detected", model->name, CONFIG_NS_PERSON_LABEL);
    } else {
        // Low model threshold, so near misses reach the log; the accept score is applied here.
        ns_infer_set_param("score_thr", NS_MODEL_SCORE_THR);
    }
    static const ns_infer_listener_t listener = {.on_result = on_result, .on_running = on_running};
    ns_infer_set_listener(&listener);

    char boot_line[96];
    snprintf(boot_line, sizeof(boot_line), "%s,%s,%" PRIu32, NS_FW_VERSION, reset_reason_name(reason), ns_event_count());
    ns_storage_append("boot.csv", "firmware,reset_reason,events_before", boot_line);

    ns_debug_http_set_index(index_html_start, index_html_end - index_html_start - 1);
    ns_debug_http_add_status(status_fields);
    ns_debug_http_add_action("alert_test", act_alert_test);
    ns_debug_http_add_action("sd_test", act_sd_test);
    ns_debug_http_add_action("push_test", act_push_test);
    ns_debug_http_add_action("beep", act_beep);
    ns_debug_http_add_action("gain", act_gain);
    ns_debug_http_add_action("accept", act_accept);

    if (xTaskCreatePinnedToCore(monitor_task, "monitor", 4096, NULL, 2, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
