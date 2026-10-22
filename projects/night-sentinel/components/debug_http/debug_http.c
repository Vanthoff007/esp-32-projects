#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "img_converters.h"
#include "camera.h"
#include "debug_http.h"

// Sources: ESP-IDF v6.1 examples/protocols/http_server/simple (server setup, handlers);
// DFRobot DFR1154_Examples 5.3 CameraWebServer app_httpd.cpp (capture_handler,
// stream_handler: the multipart/x-mixed-replace MJPEG format).

static const char *TAG = "http";

#define JPEG_QUALITY 80   // frame2jpg scale: 1-100, higher = better and bigger

#define BOUNDARY "frame"
static const char *STREAM_TYPE = "multipart/x-mixed-replace;boundary=" BOUNDARY;
static const char *PART_HEADER = "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static const char *INDEX_HTML =
    "<!doctype html><html><head><meta name=viewport content='width=device-width'>"
    "<title>night-sentinel</title></head><body style='margin:0;background:#111;color:#ccc;font-family:sans-serif'>"
    "<p style='margin:8px'>night-sentinel live &middot; <a style='color:#8cf' href='/snap.jpg' download>save snapshot</a></p>"
    "<img src='/stream' style='width:100%;max-width:960px;image-rendering:pixelated'>"
    "</body></html>";

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
// open, this server task is busy, so /snap.jpg waits until the stream closes.
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

esp_err_t ns_debug_http_start(void)
{
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
        { .uri = "/stream",   .method = HTTP_GET, .handler = stream_handler },
    };
    for (int i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
    ESP_LOGI(TAG, "debug server on port %d: /, /stream, /snap.jpg", config.server_port);
    return ESP_OK;
}
