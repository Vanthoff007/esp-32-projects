#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "img_converters.h"
#include "camera.h"
#include "light.h"
#include "ir.h"
#include "debug_http.h"
#include "detector.h"
#include "esp_heap_caps.h"
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include "storage.h"

// Sources: ESP-IDF v6.1 examples/protocols/http_server/simple (server setup, handlers);
// DFRobot DFR1154_Examples 5.3 CameraWebServer app_httpd.cpp (capture_handler,
// stream_handler: the multipart/x-mixed-replace MJPEG format).

static const char *TAG = "http";

#define JPEG_QUALITY 80   // frame2jpg scale: 1-100, higher = better and bigger

#define BOUNDARY "frame"
static const char *STREAM_TYPE = "multipart/x-mixed-replace;boundary=" BOUNDARY;
static const char *PART_HEADER = "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// The page is components/debug_http/www/index.html, built into the firmware (EMBED_TXTFILES in
// CMakeLists.txt). The linker provides its start and end; the build adds a closing NUL byte.
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

// Grab the newest frame and turn it into a JPEG. The frame is released *before*
// returning, so slow network sends never hold a camera buffer.
// On success the caller owns *jpg and must free() it.
static esp_err_t grab_jpeg(uint8_t **jpg, size_t *len)
{
    camera_fb_t *fb = ns_cam_grab();
    if (fb == NULL) {
        return ESP_FAIL;
    }
    bool ok = frame2jpg(fb, JPEG_QUALITY, jpg, len);   // mallocs *jpg
    ns_cam_release(fb);
    return ok ? ESP_OK : ESP_FAIL;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static esp_err_t snap_handler(httpd_req_t *req)
{
    uint8_t *jpg = NULL;
    size_t len = 0;
    if (grab_jpeg(&jpg, &len) != ESP_OK) {
        ESP_LOGE(TAG, "snapshot failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=snap.jpg");
    esp_err_t err = httpd_resp_send(req, (const char *)jpg, len);
    free(jpg);
    return err;
}

// Sends JPEGs one after another until the viewer disconnects. While a stream is
// open, its server task is busy; that's why it has its own server on port 81.
static esp_err_t stream_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, STREAM_TYPE);
    ESP_LOGI(TAG, "stream started");

    char part[96];
    int frames = 0;
    int64_t t0 = esp_log_timestamp();
    esp_err_t err = ESP_OK;

    int failures = 0;
    while (err == ESP_OK) {
        uint8_t *jpg = NULL;
        size_t len = 0;
        if (grab_jpeg(&jpg, &len) != ESP_OK) {
            // Without a send we can't notice the viewer leaving, so don't retry forever:
            // that would keep this (only) server task busy and lock out every other request.
            if (++failures >= 20) {
                ESP_LOGE(TAG, "stream: 20 failed grabs in a row, closing");
                break;
            }
            ESP_LOGW(TAG, "frame skipped");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        failures = 0;
        int n = snprintf(part, sizeof(part), PART_HEADER, (unsigned)len);
        err = httpd_resp_send_chunk(req, part, n);
        if (err == ESP_OK) {
            err = httpd_resp_send_chunk(req, (const char *)jpg, len);
        }
        free(jpg);

        if (++frames % 50 == 0) {
            int64_t ms = esp_log_timestamp() - t0;
            ESP_LOGI(TAG, "stream: %d frames, %.1f fps, last JPEG %u bytes",
                     frames, frames * 1000.0 / ms, (unsigned)len);
        }
    }
    ESP_LOGI(TAG, "stream closed after %d frames", frames);
    return ESP_OK;   // the viewer went away; that's normal, not an error
}

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


// ---- Day 4+: detector test and test hooks ----

static ns_debug_status_cb_t s_status_cb = NULL;

#define MAX_ACTIONS 12
static struct {
    const char *name;
    ns_debug_action_t fn;
} s_actions[MAX_ACTIONS];
static int s_n_actions = 0;

void ns_debug_http_set_status_cb(ns_debug_status_cb_t cb)
{
    s_status_cb = cb;
}

void ns_debug_http_add_action(const char *name, ns_debug_action_t fn)
{
    if (s_n_actions < MAX_ACTIONS) {
        s_actions[s_n_actions].name = name;
        s_actions[s_n_actions].fn = fn;
        s_n_actions++;
    }
}

#define DETECT_MAX_BODY (400 * 1024)

// POST /detect, body = one JPEG. Runs the person detector on it and answers with JSON:
//   {"w":320,"h":240,"ms":131,"boxes":[[0.78,112,40,190,236], ...]}
// tools/eval_detector.py sends the Day 3 test images through this.
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
    ns_det_results_t res;
    int w = 0, h = 0;
    esp_err_t err = ns_det_run_jpeg(buf, got, &res, &w, &h);
    heap_caps_free(buf);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "could not decode (colour baseline JPEG only)");
        return ESP_FAIL;
    }
    char json[700];
    int n = snprintf(json, sizeof(json), "{\"w\":%d,\"h\":%d,\"ms\":%lld,\"boxes\":[", w, h, res.time_us / 1000);
    for (int i = 0; i < res.count && n < (int)sizeof(json) - 60; i++) {
        n += snprintf(json + n, sizeof(json) - n, "%s[%.3f,%d,%d,%d,%d]", i ? "," : "",
                      res.box[i].score, res.box[i].x1, res.box[i].y1, res.box[i].x2, res.box[i].y2);
    }
    snprintf(json + n, sizeof(json) - n, "]}");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

// GET /action?name=alert_test&arg=1 : run a test hook registered by main.
static esp_err_t action_handler(httpd_req_t *req)
{
    char query[96], name[32] = "", arg[16] = "0";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "name", name, sizeof(name));
        httpd_query_key_value(query, "arg", arg, sizeof(arg));
    }
    static char msg[2048];   // static: too big for the server task's stack; one request at a time
    msg[0] = '\0';
    for (int i = 0; i < s_n_actions; i++) {
        if (strcmp(name, s_actions[i].name) == 0) {
            esp_err_t err = s_actions[i].fn(atoi(arg), msg, sizeof(msg));
            if (err != ESP_OK && msg[0] == '\0') snprintf(msg, sizeof(msg), "failed: %s", esp_err_to_name(err));
            httpd_resp_set_type(req, "text/plain");
            return httpd_resp_sendstr(req, msg[0] ? msg : "ok");
        }
    }
    int n = snprintf(msg, sizeof(msg), "unknown action. available:");
    for (int i = 0; i < s_n_actions; i++) n += snprintf(msg + n, sizeof(msg) - n, " %s", s_actions[i].name);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_sendstr(req, msg);
}

// GET /sd?path=ev            list a folder on the SD card (name and size per line)
// GET /sd?path=events.csv    download a file
// No path = the card's root. ".." is refused so nothing outside /sdcard can be read.
static esp_err_t sd_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    if (!ns_storage_mounted()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "no SD card mounted\n");
    }
    char query[160], rel[128] = "";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "path", rel, sizeof(rel));
    }
    if (strstr(rel, "..") != NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "bad path\n");
    }
    char path[160];
    snprintf(path, sizeof(path), "/sdcard/%s", rel);

    struct stat st;
    if (stat(path, &st) != 0) {
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_sendstr(req, "not found\n");
    }

    if (S_ISDIR(st.st_mode)) {
        // Folder: one line per entry, "name<TAB>size" (size "-" for a sub-folder).
        DIR *d = opendir(path);
        if (d == NULL) return httpd_resp_send_500(req);
        struct dirent *e;
        static char line[300], full[420];   // static: keep them off the server task's stack
        while ((e = readdir(d)) != NULL) {
            snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
            struct stat es;
            if (stat(full, &es) == 0 && S_ISDIR(es.st_mode)) {
                snprintf(line, sizeof(line), "%s/\t-\n", e->d_name);
            } else {
                snprintf(line, sizeof(line), "%s\t%ld\n", e->d_name, stat(full, &es) == 0 ? (long)es.st_size : -1L);
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

// GET /status : JSON with the IR state, the newest frame's brightness, what auto-exposure
// is doing right now, and the current settings. tools/capture_set.py stores this next to
// every image.
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

    static char json[1400];   // static: one request at a time, keeps it off the stack
    int n = snprintf(json, sizeof(json),
             "{\"ir\":%d,\"als_lux\":%.1f,\"luma\":%.1f,\"gain\":%.2f,\"exposure_lines\":%d,\"exposure_ms\":%.1f,"
             "\"gainceiling\":%d,\"gain_ctrl\":%d,\"exposure_ctrl\":%d,\"aec2\":%d,\"ae_level\":%d,"
             "\"brightness\":%d,\"contrast\":%d,\"special_effect\":%d}",
             ir_get(), als_lux, luma, gain, lines, lines * NS_CAM_LINE_US / 1000.0f,
             ns_cam_get_gain_ceiling(), s->status.agc, s->status.aec, s->status.aec2,
             s->status.ae_level, s->status.brightness, s->status.contrast, s->status.special_effect);
    // Extra fields from main (detection state, fps, ...): replace the closing brace.
    if (s_status_cb && n > 0 && n < (int)sizeof(json) - 2) {
        json[n - 1] = ',';
        int m = s_status_cb(json + n, sizeof(json) - n - 1);
        if (m > 0 && n + m < (int)sizeof(json) - 1) {
            json[n + m] = '}';
            json[n + m + 1] = '\0';
        } else {
            json[n - 1] = '}';
        }
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

esp_err_t ns_debug_http_start(void)
{
    // Server 1, port 80: page, snapshot, controls, status.
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;   // when all sockets are in use, close the oldest instead of refusing new ones
    config.stack_size = 12288;        // /detect runs the model and /action encodes JPEGs in this task
    config.max_uri_handlers = 12;
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }
    const httpd_uri_t routes[] = {
        { .uri = "/",         .method = HTTP_GET, .handler = index_handler },
        { .uri = "/snap.jpg", .method = HTTP_GET, .handler = snap_handler },
        { .uri = "/control",  .method = HTTP_GET, .handler = control_handler },
        { .uri = "/status",   .method = HTTP_GET, .handler = status_handler },
        { .uri = "/reg",      .method = HTTP_GET, .handler = reg_handler },
        { .uri = "/detect",   .method = HTTP_POST, .handler = detect_handler },
        { .uri = "/action",   .method = HTTP_GET, .handler = action_handler },
        { .uri = "/sd",       .method = HTTP_GET, .handler = sd_handler },
    };
    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    // Server 2, port 81: only the live stream. Each server runs in its own task, so an open
    // stream no longer blocks /snap.jpg, /control or /status.
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

    ESP_LOGI(TAG, "debug server: port 80 /, /snap.jpg, /control, /status, /detect, /action, /sd; port 81 /stream");
    return ESP_OK;
}
