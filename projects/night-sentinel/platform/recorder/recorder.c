// Video recorder: Motion-JPEG in an AVI file. Each video frame is one ordinary JPEG, so no
// video encoder is needed (the ESP32-S3 has none). File layout: Microsoft's AVI RIFF format
// ("AVI RIFF File Reference"): a fixed header, the frames one after another, then an index.
#include "recorder.h"

#include "sdkconfig.h"

#if CONFIG_NS_REC_ENABLED

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "debug_http.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "frame_hub.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "storage.h"

static const char *TAG = "rec";

#define REC_DIR NS_STORAGE_MOUNT "/rec"
#define FRAME_INTERVAL_US (1000000 / CONFIG_NS_REC_FPS)
#define MAX_FILE_S (CONFIG_NS_REC_SEGMENT_S > 60 ? CONFIG_NS_REC_SEGMENT_S : 60)   // also caps one event clip
#define MAX_FRAMES (MAX_FILE_S * CONFIG_NS_REC_FPS + CONFIG_NS_REC_PREROLL_S * CONFIG_NS_REC_FPS + 32)
#define RING_N (CONFIG_NS_REC_PREROLL_S * CONFIG_NS_REC_FPS + 1)
#define HEADER_LEN 224

static volatile ns_rec_mode_t s_mode =
#if CONFIG_NS_REC_BOOT_CONTINUOUS
    NS_REC_CONTINUOUS;
#elif CONFIG_NS_REC_BOOT_EVENT
    NS_REC_EVENT;
#else
    NS_REC_OFF;
#endif

// Set by ns_rec_trigger(), read by the recorder task.
static volatile int64_t s_clip_until_us = 0;
static char s_clip_tag[16] = "clip";

// The file being written. Only the recorder task touches these (the web page reads a few).
static FILE *s_f = NULL;
static char s_name[64] = "";         // file name inside /sdcard/rec
static uint32_t *s_idx = NULL;       // per frame: offset, size (for the index at the end)
static volatile int s_frames = 0;
static uint32_t s_movi_bytes;        // size of the frame area so far
static int s_w, s_h;
static int64_t s_first_t_us, s_last_t_us, s_file_start_us;
static uint32_t s_boot_id;           // tells files of different boots apart while the clock isn't set
static char s_msg[64] = "";

// Event mode: the last few seconds of JPEGs, oldest first.
static ns_jpeg_t *s_ring[RING_N];
static int s_ring_n = 0;

void ns_rec_set_mode(ns_rec_mode_t mode)
{
    if (mode < NS_REC_OFF || mode > NS_REC_EVENT) return;
    s_mode = mode;
    s_clip_until_us = 0;
}

ns_rec_mode_t ns_rec_get_mode(void)
{
    return s_mode;
}

bool ns_rec_active(void)
{
    return s_f != NULL;
}

void ns_rec_trigger(const char *tag)
{
    if (s_mode != NS_REC_EVENT) return;
    if (esp_timer_get_time() >= s_clip_until_us) {   // a new clip: remember its name
        strlcpy(s_clip_tag, tag ? tag : "clip", sizeof(s_clip_tag));
    }
    s_clip_until_us = esp_timer_get_time() + CONFIG_NS_REC_CLIP_S * 1000000LL;
}

// ---- AVI file ----------------------------------------------------------------------------

// AVI numbers are 32-bit, lowest byte first.
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

// The 224-byte header in front of the frames. Written once with zeros when the file is opened
// and again with the real numbers when it is closed.
static void build_header(uint8_t *h, uint32_t frames, uint32_t us_per_frame, uint32_t movi_bytes,
                         uint32_t file_bytes)
{
    memset(h, 0, HEADER_LEN);
    memcpy(h + 0, "RIFF", 4);   put32(h + 4, file_bytes ? file_bytes - 8 : 0);
    memcpy(h + 8, "AVI ", 4);
    memcpy(h + 12, "LIST", 4);  put32(h + 16, 192);
    memcpy(h + 20, "hdrl", 4);
    // Main header: 56 bytes of numbers.
    memcpy(h + 24, "avih", 4);  put32(h + 28, 56);
    put32(h + 32, us_per_frame);
    put32(h + 44, 0x10);        // flags: the file has an index
    put32(h + 48, frames);
    put32(h + 56, 1);           // one stream (video, no sound)
    put32(h + 64, s_w);
    put32(h + 68, s_h);
    // Stream list: header + format.
    memcpy(h + 88, "LIST", 4);  put32(h + 92, 116);
    memcpy(h + 96, "strl", 4);
    memcpy(h + 100, "strh", 4); put32(h + 104, 56);
    memcpy(h + 108, "vids", 4);
    memcpy(h + 112, "MJPG", 4);
    put32(h + 128, us_per_frame);   // frame rate = rate / scale = 1,000,000 / us_per_frame
    put32(h + 132, 1000000);
    put32(h + 140, frames);
    h[160] = s_w; h[161] = s_w >> 8;   // frame rectangle: right, bottom (16-bit each)
    h[162] = s_h; h[163] = s_h >> 8;
    memcpy(h + 164, "strf", 4); put32(h + 168, 40);
    put32(h + 172, 40);         // size of this format block
    put32(h + 176, s_w);
    put32(h + 180, s_h);
    h[184] = 1;                 // planes
    h[186] = 24;                // bits per pixel
    memcpy(h + 188, "MJPG", 4);
    put32(h + 192, s_w * s_h * 3);
    // The frames.
    memcpy(h + 212, "LIST", 4); put32(h + 216, movi_bytes);
    memcpy(h + 220, "movi", 4);
}

static void stamp(char *buf, size_t len)
{
    time_t now = time(NULL);
    if (now > 1700000000) {   // the clock has been set from the internet
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(buf, len, "%Y%m%d-%H%M%S", &tm);
    } else {
        snprintf(buf, len, "00000000-%03x%05lld", (unsigned)(s_boot_id & 0xfff), esp_timer_get_time() / 1000000);
    }
}

// Delete the oldest recordings until enough of the card is free. Names start with the date
// and time, so the alphabetically first file is the oldest.
static bool make_room(void)
{
    for (int tries = 0; tries < 200; tries++) {
        uint32_t total, free_mb;
        ns_storage_space(&total, &free_mb);
        if (total == 0) return false;
        if (free_mb >= CONFIG_NS_REC_MIN_FREE_MB) return true;
        DIR *d = opendir(REC_DIR);
        if (!d) return false;
        char oldest[64] = "";
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            size_t n = strlen(e->d_name);
            if (n < 5 || n >= sizeof(oldest) || strcasecmp(e->d_name + n - 4, ".avi") != 0) continue;
            if (strcmp(e->d_name, s_name) == 0) continue;
            if (!oldest[0] || strcmp(e->d_name, oldest) < 0) strlcpy(oldest, e->d_name, sizeof(oldest));
        }
        closedir(d);
        if (!oldest[0]) return false;   // nothing left to delete
        char path[96];
        snprintf(path, sizeof(path), REC_DIR "/%s", oldest);
        if (unlink(path) != 0) return false;
        ESP_LOGW(TAG, "card low on space: deleted the oldest recording %s", oldest);
    }
    return false;
}

static void close_file(void)
{
    if (!s_f) return;
    bool ok = s_frames > 0;
    if (ok) {
        // The index: one 16-byte entry per frame.
        uint8_t e[16];
        memcpy(e, "idx1", 4);
        put32(e + 4, s_frames * 16);
        ok = fwrite(e, 1, 8, s_f) == 8;
        for (int i = 0; i < s_frames && ok; i++) {
            memcpy(e, "00dc", 4);
            put32(e + 4, 0x10);              // every frame is a full picture
            put32(e + 8, s_idx[2 * i]);
            put32(e + 12, s_idx[2 * i + 1]);
            ok = fwrite(e, 1, 16, s_f) == 16;
        }
        long file_bytes = ftell(s_f);
        // Now the real numbers are known: write the header again.
        int64_t span = s_last_t_us - s_first_t_us;
        uint32_t us_per_frame = s_frames > 1 ? (uint32_t)(span / (s_frames - 1)) : FRAME_INTERVAL_US;
        uint8_t h[HEADER_LEN];
        build_header(h, s_frames, us_per_frame, s_movi_bytes, (uint32_t)file_bytes);
        ok = ok && fseek(s_f, 0, SEEK_SET) == 0 && fwrite(h, 1, HEADER_LEN, s_f) == HEADER_LEN;
        ESP_LOGI(TAG, "%s: %d frames, %.1f s, %ld KB%s", s_name, s_frames, span / 1e6, file_bytes / 1024,
                 ok ? "" : " (write error: file may not play)");
    }
    fclose(s_f);
    s_f = NULL;
    if (s_frames == 0) {   // never got a frame: don't leave an empty file
        char path[96];
        snprintf(path, sizeof(path), REC_DIR "/%s", s_name);
        unlink(path);
    }
    s_frames = 0;
}

static bool open_file(const char *tag, const ns_jpeg_t *first)
{
    if (!ns_storage_mounted()) {
        snprintf(s_msg, sizeof(s_msg), "no SD card");
        return false;
    }
    mkdir(REC_DIR, 0775);   // fails harmlessly if it exists
    if (!make_room()) {
        snprintf(s_msg, sizeof(s_msg), "card full (less than %d MB free)", CONFIG_NS_REC_MIN_FREE_MB);
        return false;
    }
    char when[32], path[96];
    stamp(when, sizeof(when));
    snprintf(s_name, sizeof(s_name), "%s_%s.avi", when, tag);
    snprintf(path, sizeof(path), REC_DIR "/%s", s_name);
    s_f = fopen(path, "wb");
    if (!s_f) {
        snprintf(s_msg, sizeof(s_msg), "could not create the file");
        return false;
    }
    s_w = first->width;
    s_h = first->height;
    s_frames = 0;
    s_movi_bytes = 4;   // the word "movi" itself counts
    s_file_start_us = esp_timer_get_time();
    uint8_t h[HEADER_LEN];
    build_header(h, 0, FRAME_INTERVAL_US, 0, 0);
    if (fwrite(h, 1, HEADER_LEN, s_f) != HEADER_LEN) {
        fclose(s_f);
        s_f = NULL;
        snprintf(s_msg, sizeof(s_msg), "card write error");
        return false;
    }
    snprintf(s_msg, sizeof(s_msg), "recording");
    ESP_LOGI(TAG, "recording to %s", s_name);
    return true;
}

static bool write_frame(const ns_jpeg_t *j)
{
    uint8_t head[8];
    memcpy(head, "00dc", 4);
    put32(head + 4, j->len);
    uint32_t pad = j->len & 1;   // chunks must start on an even byte
    bool ok = fwrite(head, 1, 8, s_f) == 8 && fwrite(j->buf, 1, j->len, s_f) == j->len &&
              (!pad || fputc(0, s_f) == 0);
    if (!ok) return false;
    s_idx[2 * s_frames] = s_movi_bytes;
    s_idx[2 * s_frames + 1] = j->len;
    s_movi_bytes += 8 + j->len + pad;
    if (s_frames == 0) s_first_t_us = j->t_us;
    s_last_t_us = j->t_us;
    s_frames++;
    return true;
}

// ---- the task ----------------------------------------------------------------------------

static void ring_clear(void)
{
    for (int i = 0; i < s_ring_n; i++) ns_jpeg_release(s_ring[i]);
    s_ring_n = 0;
}

// Takes over the caller's reference to j.
static void ring_push(ns_jpeg_t *j)
{
    if (s_ring_n == RING_N) {
        ns_jpeg_release(s_ring[0]);
        memmove(&s_ring[0], &s_ring[1], sizeof(s_ring[0]) * (RING_N - 1));
        s_ring_n--;
    }
    s_ring[s_ring_n++] = j;
}

static void recorder_task(void *arg)
{
    int64_t last_t_us = 0, retry_at_us = 0;
    ns_rec_mode_t file_mode = NS_REC_OFF;   // the mode the open file belongs to

    while (true) {
        ns_rec_mode_t mode = s_mode;
        int64_t now = esp_timer_get_time();
        if (s_f && (mode != file_mode || !ns_storage_mounted())) close_file();
        if (mode != NS_REC_EVENT) ring_clear();
        if (mode == NS_REC_OFF) {
            s_msg[0] = '\0';
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // Keep to the configured frame rate: sleep until the next frame is nearly due, then ask
        // the hub for a fresh one. The hub only encodes JPEGs while somebody is asking, so the
        // sleep is also what keeps the encoder idle between recorded frames.
        int64_t wait_us = last_t_us + FRAME_INTERVAL_US - 60000 - now;
        if (wait_us > 0) vTaskDelay(pdMS_TO_TICKS(wait_us / 1000));
        ns_jpeg_t *j = ns_hub_get_jpeg(ns_hub_seq(), 500);
        if (!j) continue;
        last_t_us = j->t_us;
        now = esp_timer_get_time();

        bool want_file = mode == NS_REC_CONTINUOUS || now < s_clip_until_us;
        if (!want_file) {
            close_file();
            ring_push(j);   // event mode, waiting: remember the frame for the next clip's start
            continue;
        }

        // Start a new file when needed: none open yet, or the open one is long enough.
        int max_s = mode == NS_REC_CONTINUOUS ? CONFIG_NS_REC_SEGMENT_S : MAX_FILE_S;
        if (s_f && (now - s_file_start_us >= max_s * 1000000LL || s_frames >= MAX_FRAMES - 1)) close_file();
        if (!s_f && now >= retry_at_us) {
            if (open_file(mode == NS_REC_CONTINUOUS ? "V" : s_clip_tag, j)) {
                file_mode = mode;
                for (int i = 0; i < s_ring_n; i++) write_frame(s_ring[i]);   // the seconds before the trigger
                ring_clear();
            } else {
                ESP_LOGW(TAG, "not recording: %s", s_msg);
                retry_at_us = now + 10 * 1000000LL;
            }
        }
        if (s_f && !write_frame(j)) {
            ESP_LOGW(TAG, "%s: write failed, closing", s_name);
            snprintf(s_msg, sizeof(s_msg), "card write error");
            close_file();
            retry_at_us = now + 10 * 1000000LL;
        }
        ns_jpeg_release(j);
    }
}

// ---- web server hooks --------------------------------------------------------------------

// /action?name=rec&arg=0|1|2 : recording off / continuous / event clips (until reboot).
static esp_err_t act_rec(int arg, char *msg, size_t len)
{
    if (arg < NS_REC_OFF || arg > NS_REC_EVENT) return ESP_ERR_INVALID_ARG;
    ns_rec_set_mode((ns_rec_mode_t)arg);
    snprintf(msg, len, "recording: %s", arg == 0 ? "off" : arg == 1 ? "continuous" : "event clips");
    return ESP_OK;
}

// /action?name=rec_clip : save an event clip now (event mode only), to test it.
static esp_err_t act_rec_clip(int arg, char *msg, size_t len)
{
    if (s_mode != NS_REC_EVENT) {
        snprintf(msg, len, "switch recording to 'event clips' first");
        return ESP_OK;
    }
    ns_rec_trigger("test");
    snprintf(msg, len, "clip started: %d s before now + %d s after", CONFIG_NS_REC_PREROLL_S, CONFIG_NS_REC_CLIP_S);
    return ESP_OK;
}

static int status_fields(char *buf, size_t len)
{
    return snprintf(buf, len, "\"rec\":1,\"rec_mode\":%d,\"rec_on\":%d,\"rec_file\":\"%s\",\"rec_frames\":%d,\"rec_msg\":\"%s\"",
                    s_mode, s_f != NULL, s_f ? s_name : "", s_frames, s_msg);
}

esp_err_t ns_rec_start(void)
{
    if (s_idx) return ESP_OK;
    s_boot_id = esp_random();
    s_idx = heap_caps_malloc(sizeof(uint32_t) * 2 * MAX_FRAMES, MALLOC_CAP_SPIRAM);
    if (!s_idx) return ESP_ERR_NO_MEM;
    // Stack in PSRAM: internal RAM is the scarce one on this board.
    if (xTaskCreatePinnedToCoreWithCaps(recorder_task, "recorder", 6144, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ns_debug_http_add_action("rec", act_rec);
    ns_debug_http_add_action("rec_clip", act_rec_clip);
    ns_debug_http_add_status(status_fields);
    ESP_LOGI(TAG, "recorder ready: %d fps, mode %s", CONFIG_NS_REC_FPS,
             s_mode == NS_REC_OFF ? "off" : s_mode == NS_REC_CONTINUOUS ? "continuous" : "event clips");
    return ESP_OK;
}

#else   // recorder switched off in menuconfig: the same functions, doing nothing

esp_err_t ns_rec_start(void) { return ESP_OK; }
void ns_rec_set_mode(ns_rec_mode_t mode) { (void)mode; }
ns_rec_mode_t ns_rec_get_mode(void) { return NS_REC_OFF; }
void ns_rec_trigger(const char *tag) { (void)tag; }
bool ns_rec_active(void) { return false; }

#endif
