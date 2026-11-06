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

// Sources: ESP-IDF v6.1 examples/protocols/http_server/simple (server setup, handlers);
// DFRobot DFR1154_Examples 5.3 CameraWebServer app_httpd.cpp (capture_handler,
// stream_handler: the multipart/x-mixed-replace MJPEG format).

static const char *TAG = "http";

#define JPEG_QUALITY 80   // frame2jpg scale: 1-100, higher = better and bigger

#define BOUNDARY "frame"
static const char *STREAM_TYPE = "multipart/x-mixed-replace;boundary=" BOUNDARY;
static const char *PART_HEADER = "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// The live view comes from the second server on port 81, so the buttons and the status line
// (port 80) keep working while it streams.
static const char *INDEX_HTML =
    "<!doctype html><html><head><meta name=viewport content='width=device-width'>"
    "<title>night-sentinel</title><style>"
    "body{margin:0;background:#111;color:#ccc;font-family:sans-serif}a{color:#8cf}"
    ".bar{margin:8px;display:flex;flex-wrap:wrap;gap:6px 12px;align-items:center}"
    "button,select{background:#222;color:#ddd;border:1px solid #444;padding:4px 8px}"
    "#st{font:12px monospace;margin:0 8px 8px}</style></head><body>"
    "<div class=bar>night-sentinel <a href='/snap.jpg' download>save snapshot</a>"
    "<span>IR <button onclick=\"c('ir',1)\">on</button><button onclick=\"c('ir',0)\">off</button></span>"
    "<label>gain ceiling <select onchange=\"c('gainceiling',this.value)\">"
    "<option>2<option>4<option>8<option>16<option>32<option>63</select></label>"
    "<label>AE level <select onchange=\"c('ae_level',this.value)\">"
    "<option>-2<option>-1<option selected>0<option>1<option>2</select></label>"
    "<label>brightness <select onchange=\"c('brightness',this.value)\">"
    "<option>-2<option>-1<option selected>0<option>1<option>2</select></label>"
    "<label>AEC2 <select onchange=\"c('aec2',this.value)\"><option value=0>off<option value=1>on</select></label>"
    "<label>grey <select onchange=\"c('special_effect',this.value)\"><option value=0>off<option value=2>on</select></label>"
    "</div><div id=st>...</div>"
    "<img id=v style='width:100%;max-width:960px'>"
    "<script>"
    "v.src='http://'+location.hostname+':81/stream';"
    "function c(k,x){fetch('/control?var='+k+'&val='+x).then(s)}"
    "function s(){fetch('/status').then(r=>r.json()).then(j=>{st.textContent="
    "'IR '+(j.ir?'ON':'off')+' | '+j.als_lux+' lux | luma '+j.luma+' | gain '+j.gain+'x (ceiling '+j.gainceiling+'x) | exposure '"
    "+j.exposure_ms+' ms | AE level '+j.ae_level+' | brightness '+j.brightness})}"
    "s();setInterval(s,2000)"
    "</script></body></html>";

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
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
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

    char json[400];
    snprintf(json, sizeof(json),
             "{\"ir\":%d,\"als_lux\":%.1f,\"luma\":%.1f,\"gain\":%.2f,\"exposure_lines\":%d,\"exposure_ms\":%.1f,"
             "\"gainceiling\":%d,\"gain_ctrl\":%d,\"exposure_ctrl\":%d,\"aec2\":%d,\"ae_level\":%d,"
             "\"brightness\":%d,\"contrast\":%d,\"special_effect\":%d}",
             ir_get(), als_lux, luma, gain, lines, lines * NS_CAM_LINE_US / 1000.0f,
             ns_cam_get_gain_ceiling(), s->status.agc, s->status.aec, s->status.aec2,
             s->status.ae_level, s->status.brightness, s->status.contrast, s->status.special_effect);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

esp_err_t ns_debug_http_start(void)
{
    // Server 1, port 80: page, snapshot, controls, status.
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;   // when all sockets are in use, close the oldest instead of refusing new ones
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

    ESP_LOGI(TAG, "debug server: port 80 /, /snap.jpg, /control, /status; port 81 /stream");
    return ESP_OK;
}
