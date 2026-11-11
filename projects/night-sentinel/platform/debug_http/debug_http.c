#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "camera.h"
#include "debug_http.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "frame_hub.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ir.h"
#include "light.h"
#include "night.h"
#include "sdkconfig.h"
#include "storage.h"

// Sources: ESP-IDF v6.1 examples/protocols/http_server/simple (server setup, handlers) and
// file_serving (upload); DFRobot DFR1154_Examples 5.3 CameraWebServer app_httpd.cpp
// (the multipart/x-mixed-replace MJPEG format).

static const char *TAG = "http";

#define BOUNDARY "frame"
static const char *STREAM_TYPE = "multipart/x-mixed-replace;boundary=" BOUNDARY;
static const char *PART_HEADER = "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ---- what other components plugged in ----------------------------------------------------

static const char *s_index = "<!doctype html><title>ESP32</title><p>No page set. Try "
                             "<a href=/status>/status</a>, <a href=/snap.jpg>/snap.jpg</a>, <a href=/sd>/sd</a>.";
static size_t s_index_len = 0;   // 0 = use strlen

#define MAX_STATUS 6
static ns_debug_status_cb_t s_status_cbs[MAX_STATUS];
static int s_n_status = 0;

#define MAX_ACTIONS 20
static struct {
    const char *name;
    ns_debug_action_t fn;
} s_actions[MAX_ACTIONS];
static int s_n_actions = 0;

#define MAX_ROUTES 6
static httpd_uri_t s_routes[MAX_ROUTES];
static int s_n_routes = 0;

#if CONFIG_NS_STREAM_ENABLED
#define STREAM_BUILT 1
#else
#define STREAM_BUILT 0
#endif
static volatile bool s_stream_on = true;

// Reply buffers for /status and /action, allocated once in PSRAM by ns_debug_http_start().
#define JSON_LEN 2400
#define MSG_LEN 2048
static char *s_json_buf, *s_msg_buf;

void ns_debug_http_set_index(const char *html, size_t len)
{
    s_index = html;
    s_index_len = len;
}

void ns_debug_http_add_status(ns_debug_status_cb_t cb)
{
    if (s_n_status < MAX_STATUS) s_status_cbs[s_n_status++] = cb;
}

void ns_debug_http_add_action(const char *name, ns_debug_action_t fn)
{
    if (s_n_actions < MAX_ACTIONS) {
        s_actions[s_n_actions].name = name;
        s_actions[s_n_actions].fn = fn;
        s_n_actions++;
    }
}

void ns_debug_http_add_route(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    if (s_n_routes < MAX_ROUTES) {
        s_routes[s_n_routes++] = (httpd_uri_t){.uri = uri, .method = method, .handler = handler};
    }
}

void ns_debug_http_stream_enable(bool on)
{
    s_stream_on = on;
}

bool ns_debug_http_stream_enabled(void)
{
    return s_stream_on;
}

// ---- page, snapshot, stream --------------------------------------------------------------

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, s_index, s_index_len ? (ssize_t)s_index_len : HTTPD_RESP_USE_STRLEN);
}

static esp_err_t snap_handler(httpd_req_t *req)
{
    // A frame taken after this request arrived, from the frame hub (shared with the stream
    // and the recorder: one JPEG encode serves them all).
    ns_jpeg_t *jpg = ns_hub_get_jpeg(ns_hub_seq(), 3000);
    if (!jpg) {
        ESP_LOGE(TAG, "snapshot failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=snap.jpg");
    esp_err_t err = httpd_resp_send(req, (const char *)jpg->buf, jpg->len);
    ns_jpeg_release(jpg);
    return err;
}

#if CONFIG_NS_STREAM_ENABLED
// Sends JPEGs one after another until the viewer disconnects. While a stream is open, its
// server task is busy; that's why it has its own server on port 81.
static esp_err_t stream_handler(httpd_req_t *req)
{
    if (!s_stream_on) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "live stream is switched off\n");
    }
    httpd_resp_set_type(req, STREAM_TYPE);
    ESP_LOGI(TAG, "stream started");

    char part[96];
    int frames = 0, timeouts = 0;
    int64_t t0 = esp_log_timestamp();
    esp_err_t err = ESP_OK;
    uint32_t seq = ns_hub_seq();

    while (err == ESP_OK && s_stream_on) {
        ns_jpeg_t *jpg = ns_hub_get_jpeg(seq, 1000);
        if (!jpg) {
            // Without a send we can't notice the viewer leaving, so don't wait forever.
            if (++timeouts >= 10) {
                ESP_LOGE(TAG, "stream: no frames for 10 s, closing");
                break;
            }
            continue;
        }
        timeouts = 0;
        seq = jpg->seq;
        int n = snprintf(part, sizeof(part), PART_HEADER, (unsigned)jpg->len);
        err = httpd_resp_send_chunk(req, part, n);
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(req, (const char *)jpg->buf, jpg->len);
        }
        if (++frames % 50 == 0) {
            int64_t ms = esp_log_timestamp() - t0;
            ESP_LOGI(TAG, "stream: %d frames, %.1f fps, last JPEG %u bytes",
                     frames, frames * 1000.0 / ms, (unsigned)jpg->len);
        }
        ns_jpeg_release(jpg);
    }
    ESP_LOGI(TAG, "stream closed after %d frames", frames);
    return ESP_OK;   // the viewer went away; that's normal, not an error
}

// /action?name=stream&arg=0|1 : switch the live stream off / on.
static esp_err_t act_stream(int arg, char *msg, size_t len)
{
    s_stream_on = arg != 0;
    snprintf(msg, len, "live stream %s", s_stream_on ? "allowed" : "off");
    return ESP_OK;
}
#endif

// ---- camera controls ---------------------------------------------------------------------

// GET /control?var=NAME&val=NUMBER : change one camera or IR setting.
// Same URL style as DFRobot's CameraWebServer.
static esp_err_t control_handler(httpd_req_t *req)
{
    char query[64];
    char var[24];
    char val_str[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "var", var, sizeof(var)) != ESP_OK ||
        httpd_query_key_value(query, "val", val_str, sizeof(val_str)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "use /control?var=NAME&val=NUMBER");
        return ESP_FAIL;
    }
    int val = atoi(val_str);
    sensor_t *s = esp_camera_sensor_get();
    int res = 0;   // the driver's setters return 0 on success

    if (strcmp(var, "ir") == 0) {
        ir_set(val != 0);
    } else if (strcmp(var, "gain_ctrl") == 0) {            // 1 = auto gain
        res = s->set_gain_ctrl(s, val);
    } else if (strcmp(var, "exposure_ctrl") == 0) {        // 1 = auto exposure
        res = s->set_exposure_ctrl(s, val);
    } else if (strcmp(var, "aec2") == 0) {
        res = s->set_aec2(s, val);
    } else if (strcmp(var, "ae_level") == 0) {             // -2..2, auto-exposure target
        res = s->set_ae_level(s, val);
    } else if (strcmp(var, "brightness") == 0) {           // -2..2
        res = s->set_brightness(s, val);
    } else if (strcmp(var, "contrast") == 0) {             // -2..2
        res = s->set_contrast(s, val);
    } else if (strcmp(var, "special_effect") == 0) {       // 0 none, 2 greyscale
        res = s->set_special_effect(s, val);
    } else if (strcmp(var, "agc_gain") == 0) {             // manual gain, needs gain_ctrl=0
        res = s->set_agc_gain(s, val);
    } else if (strcmp(var, "aec_value") == 0) {            // manual exposure, needs exposure_ctrl=0
        res = s->set_aec_value(s, val);
    } else if (strcmp(var, "gainceiling") == 0) {          // multiplier 1..63 (not the driver enum)
        res = ns_cam_set_gain_ceiling(val) != ESP_OK;
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown var");
        return ESP_FAIL;
    }
    if (res != 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "control %s = %d", var, val);
    return httpd_resp_sendstr(req, "ok");
}

// GET /reg?addr=0x3a14            : read one sensor register
// GET /reg?addr=0x3a14&val=0x06   : write it, then read it back
// Debug only: lets register experiments run without reflashing. Values reset at reboot.
static esp_err_t reg_handler(httpd_req_t *req)
{
    char query[48];
    char addr_str[12];
    char val_str[12];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "addr", addr_str, sizeof(addr_str)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "use /reg?addr=0x3a14[&val=0x06]");
        return ESP_FAIL;
    }
    // strtol with base 0 understands both "0x3a14" (hex) and "14868" (decimal).
    int addr = (int)strtol(addr_str, NULL, 0);
    sensor_t *s = esp_camera_sensor_get();
    if (httpd_query_key_value(query, "val", val_str, sizeof(val_str)) == ESP_OK) {
        int val = (int)strtol(val_str, NULL, 0);
        if (s->set_reg(s, addr, 0xff, val) != 0) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "reg 0x%04x <- 0x%02x", addr, val);
    }
    char out[48];
    snprintf(out, sizeof(out), "{\"addr\":\"0x%04x\",\"val\":%d}", addr, s->get_reg(s, addr, 0xff));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, out);
}

// ---- actions -----------------------------------------------------------------------------

// GET /action?name=alert_test&arg=1 : run a command registered with ns_debug_http_add_action.
static esp_err_t action_handler(httpd_req_t *req)
{
    char query[96], name[32] = "", arg[16] = "0";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "name", name, sizeof(name));
        httpd_query_key_value(query, "arg", arg, sizeof(arg));
    }
    char *msg = s_msg_buf;   // shared buffer in PSRAM: one request at a time
    const size_t msg_len = MSG_LEN;
    msg[0] = '\0';
    for (int i = 0; i < s_n_actions; i++) {
        if (strcmp(name, s_actions[i].name) == 0) {
            esp_err_t err = s_actions[i].fn(atoi(arg), msg, msg_len);
            if (err != ESP_OK && msg[0] == '\0') snprintf(msg, msg_len, "failed: %s", esp_err_to_name(err));
            httpd_resp_set_type(req, "text/plain");
            return httpd_resp_sendstr(req, msg[0] ? msg : "ok");
        }
    }
    int n = snprintf(msg, msg_len, "unknown action. available:");
    for (int i = 0; i < s_n_actions; i++) n += snprintf(msg + n, msg_len - n, " %s", s_actions[i].name);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_sendstr(req, msg);
}

static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));   // let the web reply go out first
    esp_restart();
}

// /action?name=reboot : restart the board.
static esp_err_t act_reboot(int arg, char *msg, size_t len)
{
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 1, NULL);
    snprintf(msg, len, "rebooting...");
    return ESP_OK;
}

// /action?name=tasks : CPU use per task since boot.
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

// /action?name=ir_mode&arg=0|1|2 : auto / always on / always off until reboot.
static esp_err_t act_ir_mode(int arg, char *msg, size_t len)
{
    if (arg < 0 || arg > 2) return ESP_ERR_INVALID_ARG;
    ns_night_set_mode((ns_ir_mode_t)arg);
    ns_night_poll();
    snprintf(msg, len, "IR mode %s, IR %s", arg == 0 ? "auto" : arg == 1 ? "on" : "off", ir_get() ? "on" : "off");
    return ESP_OK;
}

// ---- SD card files -----------------------------------------------------------------------

// Browsers and tools send special characters in a URL as %XX (a space is %20). Turn them
// back into the real characters, in place.
static void url_decode(char *s)
{
    char *out = s;
    for (; *s; s++) {
        if (s[0] == '%' && s[1] && s[2]) {
            char hex[3] = {s[1], s[2], 0};
            char *end;
            long v = strtol(hex, &end, 16);
            if (end == hex + 2 && v > 0) {
                *out++ = (char)v;
                s += 2;
                continue;
            }
        }
        *out++ = *s;
    }
    *out = 0;
}

// Turns the request's ?path=... into a full path on the card. Sends the error reply itself
// and returns false if there is no card or the path tries to leave the card ("..").
static bool sd_path(httpd_req_t *req, char *rel, size_t rel_len, char *path, size_t path_len)
{
    httpd_resp_set_type(req, "text/plain");
    if (!ns_storage_mounted()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "no SD card mounted\n");
        return false;
    }
    char query[400];
    rel[0] = '\0';
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "path", rel, rel_len);
    }
    url_decode(rel);   // "My%20music" -> "My music"
    while (rel[0] == '/') memmove(rel, rel + 1, strlen(rel));   // "/ev" and "ev" mean the same
    if (strstr(rel, "..") != NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "bad path ('..' is not allowed)\n");
        return false;
    }
    size_t n = strlen(rel);
    if (n && rel[n - 1] == '/') rel[n - 1] = '\0';
    snprintf(path, path_len, NS_STORAGE_MOUNT "/%s", rel);
    return true;
}

// GET /sd?path=ev            list a folder (one line per entry: name<TAB>size, folders end in /)
// GET /sd?path=events.csv    download a file
// No path = the card's root.
static esp_err_t sd_get_handler(httpd_req_t *req)
{
    char rel[256], path[288];
    if (!sd_path(req, rel, sizeof(rel), path, sizeof(path))) return ESP_OK;

    struct stat st;
    if (rel[0] && stat(path, &st) != 0) {
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_sendstr(req, "not found\n");
    }

    if (!rel[0] || S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d == NULL) return httpd_resp_send_500(req);
        struct dirent *e;
        static char line[300], full[560];   // static: keep them off the server task's stack
        while ((e = readdir(d)) != NULL) {
            snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
            struct stat es;
            bool ok = stat(full, &es) == 0;
            if (ok && S_ISDIR(es.st_mode)) {
                snprintf(line, sizeof(line), "%s/\t-\n", e->d_name);
            } else {
                snprintf(line, sizeof(line), "%s\t%ld\n", e->d_name, ok ? (long)es.st_size : -1L);
            }
            httpd_resp_sendstr_chunk(req, line);
        }
        closedir(d);
        return httpd_resp_sendstr_chunk(req, NULL);   // NULL = end of the chunked reply
    }

    // File: send it in 4 KB pieces so a big file never has to fit in RAM.
    const char *ext = strrchr(rel, '.');
    if (ext && strcasecmp(ext, ".jpg") == 0) httpd_resp_set_type(req, "image/jpeg");
    else if (ext && strcasecmp(ext, ".csv") == 0) httpd_resp_set_type(req, "text/csv");
    else if (ext && strcasecmp(ext, ".avi") == 0) httpd_resp_set_type(req, "video/x-msvideo");
    else if (ext && strcasecmp(ext, ".txt") != 0) httpd_resp_set_type(req, "application/octet-stream");
    FILE *f = fopen(path, "rb");
    if (f == NULL) return httpd_resp_send_500(req);
    char *buf = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (buf == NULL) { fclose(f); return httpd_resp_send_500(req); }
    size_t n;
    esp_err_t err = ESP_OK;
    while ((n = fread(buf, 1, 4096, f)) > 0) {
        err = httpd_resp_send_chunk(req, buf, n);
        if (err != ESP_OK) break;   // browser/PC closed the connection
    }
    heap_caps_free(buf);
    fclose(f);
    return err == ESP_OK ? httpd_resp_send_chunk(req, NULL, 0) : err;
}

// POST /sd?path=models/my_model.espdl   body = the file. Replaces an existing file; creates
// the folder (one level) if it is missing.
static esp_err_t sd_post_handler(httpd_req_t *req)
{
    char rel[256], path[288];
    if (!sd_path(req, rel, sizeof(rel), path, sizeof(path))) return ESP_OK;
    if (!rel[0]) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "use POST /sd?path=folder/file with the file as the body\n");
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        char *slash = strrchr(path, '/');
        if (slash && slash > path + strlen(NS_STORAGE_MOUNT)) {
            *slash = '\0';
            mkdir(path, 0775);
            *slash = '/';
            f = fopen(path, "wb");
        }
    }
    char *buf = f ? heap_caps_malloc(4096, MALLOC_CAP_SPIRAM) : NULL;
    if (!buf) {
        if (f) fclose(f);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "could not create the file\n");
    }
    size_t left = req->content_len, done = 0;
    bool ok = true;
    while (left > 0 && ok) {
        int r = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { ok = false; break; }
        ok = fwrite(buf, 1, r, f) == (size_t)r;
        left -= r;
        done += r;
    }
    heap_caps_free(buf);
    ok = fclose(f) == 0 && ok;
    if (!ok) {
        unlink(path);   // don't leave half a file behind
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "upload failed (connection lost or card full)\n");
    }
    ESP_LOGI(TAG, "upload: %s, %u bytes", rel, (unsigned)done);
    char msg[300];
    snprintf(msg, sizeof(msg), "saved %s (%u bytes)\n", rel, (unsigned)done);
    return httpd_resp_sendstr(req, msg);
}

// DELETE /sd?path=rec/old.avi   remove a file, or a folder that is already empty.
static esp_err_t sd_delete_handler(httpd_req_t *req)
{
    char rel[256], path[288];
    if (!sd_path(req, rel, sizeof(rel), path, sizeof(path))) return ESP_OK;
    struct stat st;
    if (!rel[0] || stat(path, &st) != 0) {
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_sendstr(req, "not found\n");
    }
    int r = S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path);
    if (r != 0) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, S_ISDIR(st.st_mode) ? "folder is not empty\n" : "could not delete\n");
    }
    ESP_LOGI(TAG, "deleted %s", rel);
    return httpd_resp_sendstr(req, "deleted\n");
}

// ---- status ------------------------------------------------------------------------------

// GET /status : one JSON object. The fields below come from the platform; other components
// (inference engine, recorder, app) add theirs with ns_debug_http_add_status().
// tools/capture_set.py stores this next to every image.
static esp_err_t status_handler(httpd_req_t *req)
{
    float luma = -1;
    camera_fb_t *fb = ns_cam_grab();
    if (fb != NULL) {
        luma = ns_cam_mean_luma(fb);
        ns_cam_release(fb);
    }
    float gain;
    int lines;
    ns_cam_read_ae(&gain, &lines);
    sensor_t *s = esp_camera_sensor_get();
    float als_lux = -1;   // LTR-308 reading; -1 if the sensor isn't working
    ns_light_read_lux(&als_lux);
    uint32_t sd_total, sd_free;
    ns_storage_space(&sd_total, &sd_free);
    int rssi = 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) rssi = ap.rssi;
    static const char *ir_modes[] = {"auto", "on", "off"};

    char *json = s_json_buf;   // shared buffer in PSRAM: one request at a time
    const int json_len = JSON_LEN;
    int n = snprintf(json, json_len,
             "{\"ir\":%d,\"ir_mode\":\"%s\",\"als_lux\":%.1f,\"luma\":%.1f,\"gain\":%.2f,\"exposure_lines\":%d,"
             "\"exposure_ms\":%.1f,\"gainceiling\":%d,\"gain_ctrl\":%d,\"exposure_ctrl\":%d,\"aec2\":%d,"
             "\"ae_level\":%d,\"brightness\":%d,\"contrast\":%d,\"special_effect\":%d,"
             "\"sd\":%d,\"sd_total_mb\":%u,\"sd_free_mb\":%u,\"rssi\":%d,\"uptime_s\":%lld,\"stream\":%d,"
             "\"null_frames\":%u,\"free_int\":%u,\"min_int\":%u,\"free_psram\":%u",
             ir_get(), ir_modes[ns_night_get_mode()], als_lux, luma, gain, lines,
             lines * NS_CAM_LINE_US / 1000.0f, ns_cam_get_gain_ceiling(), s->status.agc, s->status.aec,
             s->status.aec2, s->status.ae_level, s->status.brightness, s->status.contrast,
             s->status.special_effect, ns_storage_mounted(), (unsigned)sd_total, (unsigned)sd_free, rssi,
             esp_timer_get_time() / 1000000, STREAM_BUILT && s_stream_on,
             (unsigned)ns_hub_null_frames(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    for (int i = 0; i < s_n_status && n < json_len - 64; i++) {
        json[n] = ',';
        int m = s_status_cbs[i](json + n + 1, json_len - n - 2);
        if (m > 0 && n + 1 + m < json_len - 1) n += 1 + m;   // else: drop this block
    }
    json[n] = '}';
    json[n + 1] = '\0';
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

// ---- start -------------------------------------------------------------------------------

esp_err_t ns_debug_http_start(void)
{
    s_json_buf = heap_caps_malloc(JSON_LEN, MALLOC_CAP_SPIRAM);
    s_msg_buf = heap_caps_malloc(MSG_LEN, MALLOC_CAP_SPIRAM);
    if (!s_json_buf || !s_msg_buf) return ESP_ERR_NO_MEM;
    ns_debug_http_add_action("reboot", act_reboot);
    ns_debug_http_add_action("tasks", act_tasks);
    ns_debug_http_add_action("ir_mode", act_ir_mode);

    // Server 1, port 80: page, snapshot, controls, status, SD card, plugged-in routes.
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;   // when all sockets are in use, close the oldest instead of refusing new ones
    config.stack_size = 12288;        // POST /detect runs the model and some actions encode JPEGs in this task
    config.max_uri_handlers = 9 + MAX_ROUTES;
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }
    const httpd_uri_t routes[] = {
        { .uri = "/",         .method = HTTP_GET,    .handler = index_handler },
        { .uri = "/snap.jpg", .method = HTTP_GET,    .handler = snap_handler },
        { .uri = "/control",  .method = HTTP_GET,    .handler = control_handler },
        { .uri = "/status",   .method = HTTP_GET,    .handler = status_handler },
        { .uri = "/reg",      .method = HTTP_GET,    .handler = reg_handler },
        { .uri = "/action",   .method = HTTP_GET,    .handler = action_handler },
        { .uri = "/sd",       .method = HTTP_GET,    .handler = sd_get_handler },
        { .uri = "/sd",       .method = HTTP_POST,   .handler = sd_post_handler },
        { .uri = "/sd",       .method = HTTP_DELETE, .handler = sd_delete_handler },
    };
    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
    for (int i = 0; i < s_n_routes; i++) {
        httpd_register_uri_handler(server, &s_routes[i]);
    }

#if CONFIG_NS_STREAM_ENABLED
    // Server 2, port 81: only the live stream. Each server runs in its own task, so an open
    // stream never blocks /snap.jpg, /control or /status.
    ns_debug_http_add_action("stream", act_stream);
    httpd_config_t stream_cfg = HTTPD_DEFAULT_CONFIG();
    stream_cfg.server_port = 81;
    stream_cfg.ctrl_port = config.ctrl_port + 1;   // each server needs its own internal port
    stream_cfg.lru_purge_enable = true;
    httpd_handle_t stream_server = NULL;
    err = httpd_start(&stream_server, &stream_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "stream server failed: %s", esp_err_to_name(err));
        return err;
    }
    const httpd_uri_t stream_route = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler };
    httpd_register_uri_handler(stream_server, &stream_route);
#endif

    ESP_LOGI(TAG, "web server: port 80 (%d extra route%s, %d actions)%s", s_n_routes,
             s_n_routes == 1 ? "" : "s", s_n_actions,
             STREAM_BUILT ? ", port 81 /stream" : ", live stream not built in");
    return ESP_OK;
}
