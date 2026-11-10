// Night Sentinel: camera -> person detector -> intrusion state machine -> speaker + phone push
// + SD evidence. Plan sections 1, 2 and Day 10.
//
// Tasks:
//   detect   core 1, prio 5   owns the camera and the model; runs the state machine; on an
//                             intrusion encodes one JPEG and hands the event to the others
//   audio    core 0           plays the warning clip       (components/alerts)
//   notify   core 0           HTTPS push to the phone      (components/net/notify_ntfy.c)
//   storage  core 0           JPEG + CSV on the SD card    (components/storage)
//   monitor  core 0, prio 2   IR day/night switch every 2 s, heartbeat every 60 s
//   led      core 0, prio 1   status LED pattern
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "camera.h"
#include "debug_http.h"
#include "detector.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "intrusion_config.h"
#include "intrusion_fsm.h"
#include "ir.h"
#include "light.h"
#include "notify.h"
#include "ns_event.h"
#include "ns_mdns.h"
#include "ns_time.h"
#include "nvs_flash.h"
#include "status_led.h"
#include "storage.h"
#include "wifi.h"

#define NS_FW_VERSION "0.9.0"
#define EVENT_JPEG_QUALITY 80
#define TIMING_EVERY_FRAMES 50   // Day 5: one stage-timing summary line per 50 frames

static const char *TAG = "main";

// ---------------------------------------------------------------------------------------------
// Shared state. Written by one task, read by others (status page, heartbeat). Single values
// of 32 bits or less are read/written in one go on this CPU, so no lock is needed for them.

typedef enum { LED_MONITORING, LED_ALERT, LED_ERROR } led_mode_t;
static volatile led_mode_t s_led_mode = LED_MONITORING;

static volatile fsm_state_t s_state = ST_IDLE;
static volatile float s_fps = 0;            // detection loop rate, averaged per timing window
static volatile float s_infer_ms = 0;       // average model time in the same window
static volatile uint32_t s_frames = 0;      // frames processed since boot
static volatile uint32_t s_null_frames = 0; // grabs that returned no frame
static volatile float s_last_best = 0;      // best box score on the latest frame (any box)
static volatile uint32_t s_alerts = 0;      // alerts fired since boot (real + test)
static volatile float s_lux = -1;

// Web page controls. Both go back to the defaults on every reboot, so a power cut can never
// leave the alarm switched off.
static volatile bool s_detect_on = true;    // false = model not run, no alerts
static volatile int s_accept_pct = -1;      // accept score override in %, -1 = intrusion_config.h
// Best box of the latest frame, for the box drawn over the live view: x1,y1,x2,y2,accepted.
static volatile int s_last_box[5] = {0};

typedef enum { IR_MODE_AUTO, IR_MODE_ON, IR_MODE_OFF } ir_mode_t;
static volatile ir_mode_t s_ir_mode =
#if CONFIG_NS_IR_ALWAYS_ON
    IR_MODE_ON;
#elif CONFIG_NS_IR_ALWAYS_OFF
    IR_MODE_OFF;
#else
    IR_MODE_AUTO;
#endif

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---------------------------------------------------------------------------------------------
// Events: one JPEG, then fan out to speaker, phone and SD card.

// Takes ownership of jpg. Called from the detect task (real) or the web server (tests).
static void fire_event(uint8_t *jpg, size_t jpg_len, float score, const int box[4], bool test,
                       bool speak)
{
    ns_event_t *ev = ns_event_new(jpg, jpg_len);
    if (!ev) return;
    ev->score = score;
    memcpy(ev->box, box, sizeof(ev->box));
    ev->ir = ir_get();
    ev->lux = s_lux;
    ev->test = test;
    s_alerts++;

    char name[12];
    ns_event_name(ev, name, sizeof(name));
    bool spoke = speak && ns_audio_play(CLIP_WARN);
    ESP_LOGW(TAG, "INTRUSION %s%s: score %.2f box %d,%d-%d,%d, jpeg %u bytes, speaker %s",
             name, test ? " (test)" : "", score, box[0], box[1], box[2], box[3],
             (unsigned)jpg_len, spoke ? "on" : "busy/off");
    ns_notify_send(ev);          // each of these takes its own reference ...
    ns_storage_save_event(ev);
    ns_event_release(ev);        // ... so we can drop ours now
}

// ---------------------------------------------------------------------------------------------
// The detection loop (core 1).

static void print_frame_csv(uint32_t t, const ns_det_results_t *res)
{
#if CONFIG_NS_FRAME_CSV
    char line[48 + NS_DET_MAX_BOXES * 32];
    int n = snprintf(line, sizeof(line), "F,%" PRIu32 ",%d", t, res->count);
    for (int i = 0; i < res->count && n < (int)sizeof(line) - 32; i++) {
        n += snprintf(line + n, sizeof(line) - n, ",%.2f,%d,%d,%d,%d", res->box[i].score,
                      res->box[i].x1, res->box[i].y1, res->box[i].x2, res->box[i].y2);
    }
    puts(line);   // plain line without a log prefix, so tools can split it easily
#endif
}

static void detect_task(void *arg)
{
    intrusion_fsm_t fsm;
    fsm_params_t params = fsm_default_params();
#if CONFIG_NS_DEMO_MODE
    params.cooldown_ms = NS_DEMO_COOLDOWN_MS;
#endif
    fsm_init(&fsm, &params);
    esp_task_wdt_add(NULL);   // reset the chip if this loop ever hangs for 10 s

    ESP_LOGI(TAG, "monitoring started %.1f s after boot", esp_timer_get_time() / 1e6);

    // Day 5 stage timings, summed over TIMING_EVERY_FRAMES frames.
    int64_t sum_grab = 0, sum_infer = 0, sum_logic = 0;
    int win_frames = 0;
    int64_t win_start = esp_timer_get_time();
    bool paused = false;
    const float default_accept = params.accept_score;

    while (true) {
        esp_task_wdt_reset();

        // Stopped from the web page: don't run the model, forget any half-confirmed person.
        if (!s_detect_on) {
            if (!paused) {
                paused = true;
                printf("S,%" PRIu32 ",%s,PAUSED,%" PRIu32 "\n", now_ms(), fsm_state_name(fsm_state(&fsm)), ns_event_count());
                ESP_LOGW(TAG, "detection stopped from the web page");
                fsm_init(&fsm, &params);   // back to IDLE with an empty history
                s_state = ST_IDLE;
                s_fps = 0;
                s_last_best = 0;
                s_last_box[0] = s_last_box[1] = s_last_box[2] = s_last_box[3] = s_last_box[4] = 0;
                s_led_mode = LED_MONITORING;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (paused) {
            paused = false;
            printf("S,%" PRIu32 ",PAUSED,%s,%" PRIu32 "\n", now_ms(), fsm_state_name(fsm_state(&fsm)), ns_event_count());
            ESP_LOGW(TAG, "detection started from the web page");
            sum_grab = sum_infer = sum_logic = 0;   // start a fresh timing window
            win_frames = 0;
            win_start = esp_timer_get_time();
        }
        params.accept_score = s_accept_pct >= 0 ? s_accept_pct / 100.0f : default_accept;

        int64_t t0 = esp_timer_get_time();
        camera_fb_t *fb = ns_cam_grab();
        int64_t t1 = esp_timer_get_time();
        if (!fb) {
            s_null_frames++;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        ns_det_results_t res;
        if (ns_det_run((const uint16_t *)fb->buf, fb->width, fb->height, true, &res) != ESP_OK) {
            res.count = 0;
            res.time_us = 0;
        }
        int64_t t2 = esp_timer_get_time();

        // Is there a person? The best box that passes the score / size filters.
        bool hit = false;
        float best = 0, best_any = 0;
        int best_box[4] = {0};
        int any_box[4] = {0};
        for (int i = 0; i < res.count; i++) {
            const ns_det_box_t *b = &res.box[i];
            if (b->score > best_any) {
                best_any = b->score;
                any_box[0] = b->x1, any_box[1] = b->y1, any_box[2] = b->x2, any_box[3] = b->y2;
            }
            if (fsm_box_accepted(&params, b->score, b->x1, b->y1, b->x2, b->y2, fb->width, fb->height) &&
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

        uint32_t t = now_ms();
        fsm_state_t before = fsm_state(&fsm);
        fsm_action_t act = fsm_step(&fsm, (fsm_input_t){.hit = hit, .score = best, .now_ms = t});
        fsm_state_t after = fsm_state(&fsm);

        if (act & ACT_ALERT) {
            // Encode the frame that confirmed the event, before the camera reuses its buffer.
            uint8_t *jpg = NULL;
            size_t jpg_len = 0;
            if (!frame2jpg(fb, EVENT_JPEG_QUALITY, &jpg, &jpg_len)) {
                jpg = NULL;
                jpg_len = 0;
            }
            fire_event(jpg, jpg_len, best, best_box, false, true);
        }
        ns_cam_release(fb);

        print_frame_csv(t, &res);
        if (after != before) {
            printf("S,%" PRIu32 ",%s,%s,%" PRIu32 "\n", t, fsm_state_name(before), fsm_state_name(after),
                   ns_event_count());
            s_state = after;
            s_led_mode = (after == ST_CONFIRMED || after == ST_ALERT_ACTIVE) ? LED_ALERT : LED_MONITORING;
        }
        if (act & ACT_LOG_REAPPEAR) ESP_LOGI(TAG, "person seen again during cooldown (no new alert)");
        if (act & ACT_SUPPRESSED_BY_CAP) ESP_LOGW(TAG, "alert suppressed: %d alerts in the last hour", params.max_alerts_per_hour);
        int64_t t3 = esp_timer_get_time();

        s_frames++;
        sum_grab += t1 - t0;
        sum_infer += res.time_us;
        sum_logic += (t3 - t2) + ((t2 - t1) - res.time_us);   // everything that isn't grab or model
        if (++win_frames == TIMING_EVERY_FRAMES) {
            float secs = (t3 - win_start) / 1e6f;
            s_fps = win_frames / secs;
            s_infer_ms = sum_infer / 1000.0f / win_frames;
            printf("T,frames=%d,grab_ms=%.1f,infer_ms=%.1f,other_ms=%.1f,loop_ms=%.1f,fps=%.2f\n",
                   win_frames, sum_grab / 1000.0f / win_frames, s_infer_ms,
                   sum_logic / 1000.0f / win_frames, secs * 1000 / win_frames, s_fps);
            sum_grab = sum_infer = sum_logic = 0;
            win_frames = 0;
            win_start = t3;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Status LED: slow blink = monitoring, solid = alert active, fast blink = error.

static void led_task(void *arg)
{
    bool on = false;
    int tick = 0;
    while (true) {
        switch (s_led_mode) {
        case LED_ALERT: on = true; break;
        case LED_ERROR: on = !on; break;                    // 5 Hz
        case LED_MONITORING: on = (tick % 20) == 0; break;  // 100 ms flash every 2 s
        }
        status_led_set(on);
        tick++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ---------------------------------------------------------------------------------------------
// Monitor task: IR day/night switching and the heartbeat.

static void set_ir(bool on)
{
    if (on == ir_get()) return;
    ir_set(on);
    // Night mode (AEC2) lets the sensor stretch exposure up to 180 ms: the Day 3 sweep's best
    // setting in the dark, but it would cost frame rate in daylight, so it follows the IR.
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_aec2(s, on ? 1 : 0);
    ESP_LOGI(TAG, "%s: IR %s (lux %.1f)", on ? "night mode" : "day mode", on ? "ON" : "off", s_lux);
}

static void update_ir(void)
{
    float lux;
    if (ns_light_read_lux(&lux) == ESP_OK) {
        s_lux = lux;
    } else {
        s_lux = -1;
    }
    switch (s_ir_mode) {
    case IR_MODE_ON: set_ir(true); return;
    case IR_MODE_OFF: set_ir(false); return;
    case IR_MODE_AUTO: break;
    }
    if (s_lux >= 0) {
        if (!ir_get() && s_lux < CONFIG_NS_IR_ON_BELOW_LUX) set_ir(true);
        else if (ir_get() && s_lux > CONFIG_NS_IR_OFF_ABOVE_LUX) set_ir(false);
    } else {
        // No light sensor: use the sensor's own gain. In the dark auto-exposure runs it up to
        // the 16x ceiling; in room light it sits near 2-7x.
        float gain;
        int lines;
        ns_cam_read_ae(&gain, &lines);
        if (!ir_get() && gain >= 15.5f) set_ir(true);
        else if (ir_get() && gain <= 4.0f) set_ir(false);
    }
}

#define HEARTBEAT_HEADER "uptime_s,fps,infer_ms,free_int,min_int,free_psram,min_psram,rssi,events,state,ir,lux,sd_free_mb,null_frames"

static void heartbeat(void)
{
    int rssi = 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;
    uint32_t sd_total, sd_free;
    ns_storage_space(&sd_total, &sd_free);
    char line[200];
    snprintf(line, sizeof(line), "%lld,%.2f,%.1f,%u,%u,%u,%u,%d,%" PRIu32 ",%s,%d,%.1f,%" PRIu32 ",%" PRIu32,
             esp_timer_get_time() / 1000000, s_fps, s_infer_ms,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM), rssi, s_alerts,
             fsm_state_name(s_state), ir_get(), s_lux, sd_free, s_null_frames);
    printf("H,%" PRIu32 ",%s\n", now_ms(), line);
    ns_storage_append("heartbeat.csv", HEARTBEAT_HEADER, line);
    if (sd_total && sd_free * 10 < sd_total) {
        ESP_LOGW(TAG, "SD card almost full: %" PRIu32 " of %" PRIu32 " MB free", sd_free, sd_total);
    }
}

static void monitor_task(void *arg)
{
    int64_t last_hb = esp_timer_get_time();
    while (true) {
        update_ir();
        if (CONFIG_NS_HEARTBEAT_S > 0 && esp_timer_get_time() - last_hb >= CONFIG_NS_HEARTBEAT_S * 1000000LL) {
            last_hb = esp_timer_get_time();
            heartbeat();
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ---------------------------------------------------------------------------------------------
// Debug web server hooks: extra /status fields and /action test commands.

static int status_fields(char *buf, size_t len)
{
    static const char *ir_modes[] = {"auto", "on", "off"};
    uint32_t sd_total, sd_free;
    ns_storage_space(&sd_total, &sd_free);
    int rssi = 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;
    fsm_params_t p = fsm_default_params();
    float accept = s_accept_pct >= 0 ? s_accept_pct / 100.0f : p.accept_score;
    int n = snprintf(buf, len,
                     "\"detect\":%d,\"accept\":%.2f,\"box\":[%d,%d,%d,%d],\"box_ok\":%d,"
                     "\"sd_total_mb\":%" PRIu32 ",\"sd_free_mb\":%" PRIu32 ",\"rssi\":%d,",
                     s_detect_on, accept, s_last_box[0], s_last_box[1], s_last_box[2], s_last_box[3],
                     s_last_box[4], sd_total, sd_free, rssi);
    if (n < 0 || n >= (int)len) return 0;
    buf += n;
    len -= n;
    return n + snprintf(buf, len,
                    "\"fw\":\"%s\",\"params\":\"%s\",\"uptime_s\":%lld,\"state\":\"%s\",\"fps\":%.2f,"
                    "\"infer_ms\":%.1f,\"frames\":%" PRIu32 ",\"best_score\":%.2f,\"alerts\":%" PRIu32 ","
                    "\"event_count\":%" PRIu32 ",\"ir_mode\":\"%s\",\"sd\":%d,\"push\":%d,\"time_synced\":%d,"
                    "\"audio_gain\":%d,\"free_int\":%u,\"min_int\":%u,\"free_psram\":%u",
                    NS_FW_VERSION, NS_PARAMS_VERSION, esp_timer_get_time() / 1000000,
                    fsm_state_name(s_state), s_fps, s_infer_ms, s_frames, s_last_best, s_alerts,
                    ns_event_count(), ir_modes[s_ir_mode], ns_storage_mounted(), ns_notify_configured(),
                    ns_time_synced(), ns_audio_get_gain(),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                    (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
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

static bool camera_jpeg(uint8_t **jpg, size_t *len)
{
    camera_fb_t *fb = ns_cam_grab();
    if (!fb) return false;
    bool ok = frame2jpg(fb, EVENT_JPEG_QUALITY, jpg, len);
    ns_cam_release(fb);
    return ok;
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

// /action?name=sd_test&arg=N : N fake events straight to the SD card (Day 9: 100 writes).
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
        ns_storage_save_event(ev);
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

// /action?name=ir_mode&arg=0|1|2 : auto / always on / always off until reboot.
static esp_err_t act_ir_mode(int arg, char *msg, size_t len)
{
    if (arg < 0 || arg > 2) return ESP_ERR_INVALID_ARG;
    s_ir_mode = (ir_mode_t)arg;
    update_ir();
    snprintf(msg, len, "IR mode %s, IR %s", arg == 0 ? "auto" : arg == 1 ? "on" : "off", ir_get() ? "on" : "off");
    return ESP_OK;
}

// /action?name=detect&arg=0|1 : stop / start person detection (always on again after a reboot).
static esp_err_t act_detect(int arg, char *msg, size_t len)
{
    s_detect_on = arg != 0;
    snprintf(msg, len, "person detection %s", s_detect_on ? "ON" : "STOPPED (no alerts until started or rebooted)");
    return ESP_OK;
}

// /action?name=accept&arg=30..95 : accept score in %, for live tuning. arg=0 = back to the
// value in intrusion_config.h. Not saved: a reboot restores the default.
static esp_err_t act_accept(int arg, char *msg, size_t len)
{
    if (arg != 0 && (arg < 30 || arg > 95)) return ESP_ERR_INVALID_ARG;
    s_accept_pct = arg == 0 ? -1 : arg;
    snprintf(msg, len, "accept score %s",
             arg == 0 ? "back to default" : "set (until reboot)");
    return ESP_OK;
}

static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));   // let the web reply go out first
    esp_restart();
}

// /action?name=reboot : restart the board (also clears the 10-alerts-per-hour count).
static esp_err_t act_reboot(int arg, char *msg, size_t len)
{
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 1, NULL);
    snprintf(msg, len, "rebooting...");
    return ESP_OK;
}

// /action?name=tasks : CPU use per task since boot (Day 5: core 1 should be ~90 % busy).
static esp_err_t act_tasks(int arg, char *msg, size_t len)
{
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS && CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
    if (len < 1500) return ESP_ERR_INVALID_SIZE;
    vTaskGetRunTimeStats(msg);   // writes ~40 bytes per task
#else
    snprintf(msg, len, "run-time stats not enabled in menuconfig");
#endif
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

static void fail_forever(const char *what)
{
    ESP_LOGE(TAG, "%s failed: can't monitor. Fast-blinking LED.", what);
    s_led_mode = LED_ERROR;
    xTaskCreatePinnedToCore(led_task, "led", 2048, NULL, 1, NULL, 0);
}

void app_main(void)
{
    status_led_init();
    ir_init();
    // Time zone first: after a quick reset the clock is still right (it survives in RTC
    // memory), and events in the first seconds, before Wi-Fi, must not be stamped in UTC.
    setenv("TZ", CONFIG_NS_TZ, 1);
    tzset();

    // NVS (a small key-value store in flash): Wi-Fi calibration, saved login, event counter.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    esp_reset_reason_t reason = esp_reset_reason();
    fsm_params_t p = fsm_default_params();
#if CONFIG_NS_DEMO_MODE
    p.cooldown_ms = NS_DEMO_COOLDOWN_MS;
#endif
    ESP_LOGI(TAG, "Night Sentinel %s, reset reason: %s", NS_FW_VERSION, reset_reason_name(reason));
    ESP_LOGI(TAG, "params %s: model thr %.2f, accept %.2f, min box h %.2f, max area %.2f, confirm %d/%d, "
                  "absent %" PRIu32 " ms, cooldown %" PRIu32 " ms, cap %d/h",
             NS_PARAMS_VERSION, NS_MODEL_SCORE_THR, p.accept_score, p.min_box_h, p.max_box_area,
             p.confirm_k, p.confirm_n, p.absent_ms, p.cooldown_ms, p.max_alerts_per_hour);

    if (ns_cam_init() != ESP_OK) {
        fail_forever("camera");
        return;
    }
    ns_light_init();   // on failure the IR switch falls back to the camera's gain
    if (ns_det_init(NS_MODEL_SCORE_THR) != ESP_OK) {
        fail_forever("detector");
        return;
    }
    ns_storage_start();   // never fails for a missing card
    ns_audio_init();
    ns_notify_start();    // waits for Wi-Fi per event

    char boot_line[96];
    snprintf(boot_line, sizeof(boot_line), "%s,%s,%" PRIu32, NS_FW_VERSION, reset_reason_name(reason), ns_event_count());
    ns_storage_append("boot.csv", "firmware,reset_reason,events_before", boot_line);

    // Start monitoring before Wi-Fi: detection and the speaker must work without a network.
    xTaskCreatePinnedToCore(detect_task, "detect", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(monitor_task, "monitor", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(led_task, "led", 2048, NULL, 1, NULL, 0);

    // Join Wi-Fi. Blocks until online (opening the setup hotspot if needed), then reconnects
    // by itself after any drop. Detection keeps running the whole time.
    if (ns_wifi_connect(15000) == ESP_OK) {
        ns_time_start();
        ns_mdns_start("night-sentinel");   // http://night-sentinel.local/
#if CONFIG_NS_DEBUG_HTTP
        ns_debug_http_set_status_cb(status_fields);
        ns_debug_http_add_action("alert_test", act_alert_test);
        ns_debug_http_add_action("sd_test", act_sd_test);
        ns_debug_http_add_action("push_test", act_push_test);
        ns_debug_http_add_action("beep", act_beep);
        ns_debug_http_add_action("gain", act_gain);
        ns_debug_http_add_action("ir_mode", act_ir_mode);
        ns_debug_http_add_action("tasks", act_tasks);
        ns_debug_http_add_action("detect", act_detect);
        ns_debug_http_add_action("accept", act_accept);
        ns_debug_http_add_action("reboot", act_reboot);
        ns_debug_http_start();
#endif
        ESP_LOGI(TAG, "online");
    }
}
