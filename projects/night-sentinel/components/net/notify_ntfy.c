// Phone push through ntfy.sh. Sources: plan section 11; ntfy docs (docs.ntfy.sh/publish:
// PUT body = attachment, Title/Message/Priority/Tags/Filename headers);
// ESP-IDF v6.1 esp_http_client + esp_crt_bundle (examples/protocols/esp_http_client).
#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "notify.h"
#include "ns_time.h"
#include "sdkconfig.h"

static const char *TAG = "notify";

#define QUEUE_LEN 4
#define TIMEOUT_MS 10000
#define RETRY_DELAY_MS 3000
#define WAIT_WIFI_MS 15000   // after this, give up on this event (it's still on the SD card)

static QueueHandle_t s_queue = NULL;

bool ns_notify_configured(void)
{
    return strlen(CONFIG_NS_NTFY_TOPIC) > 0;
}

static bool wifi_up(void)
{
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

// One request to ntfy. body may be NULL (text only). Returns the HTTP status, or -1.
static int put(const char *title, const char *message, const char *filename,
               const uint8_t *body, size_t body_len)
{
    char url[160];
    snprintf(url, sizeof(url), "%s/%s", CONFIG_NS_NTFY_SERVER, CONFIG_NS_NTFY_TOPIC);
    esp_http_client_config_t c = {
        .url = url,
        .method = body ? HTTP_METHOD_PUT : HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,   // trust the usual public certificate authorities
        .timeout_ms = TIMEOUT_MS,
    };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) {
        return -1;
    }
    esp_http_client_set_header(h, "Title", title);
    esp_http_client_set_header(h, "Priority", "5");
    esp_http_client_set_header(h, "Tags", "rotating_light");
    if (body) {
        // With an attachment the body is the file, so the text goes in a header.
        esp_http_client_set_header(h, "Message", message);
        esp_http_client_set_header(h, "Filename", filename);
        esp_http_client_set_post_field(h, (const char *)body, (int)body_len);
    } else {
        esp_http_client_set_post_field(h, message, (int)strlen(message));
    }
    esp_err_t err = esp_http_client_perform(h);
    int status = err == ESP_OK ? esp_http_client_get_status_code(h) : -1;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(h);
    return status;
}

int ns_notify_text(const char *title, const char *message)
{
    if (!ns_notify_configured()) {
        return -1;
    }
    return put(title, message, NULL, NULL, 0);
}

static void push_event(ns_event_t *ev)
{
    char name[12], when[40], msg[160], file[24];
    ns_event_name(ev, name, sizeof(name));
    ns_time_iso(ev->wall, when, sizeof(when));
    snprintf(msg, sizeof(msg), "%s %s score %.2f, IR %s%s", name, when, ev->score,
             ev->ir ? "on" : "off", ev->test ? " (test)" : "");
    snprintf(file, sizeof(file), "%s.jpg", name);

    int64_t t0 = esp_timer_get_time();
    // Wait for Wi-Fi: it may be reconnecting.
    while (!wifi_up() && esp_timer_get_time() - t0 < WAIT_WIFI_MS * 1000LL) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    int status = -1;
    if (wifi_up()) {
        status = put("Intruder detected", msg, file, ev->jpg, ev->jpg_len);
        if (status != 200) {
            ESP_LOGW(TAG, "%s: push failed (HTTP %d), retrying in 3 s", name, status);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
            // Second try: if the image upload is the problem, at least send the text.
            status = ev->jpg ? put("Intruder detected", msg, file, ev->jpg, ev->jpg_len) : -1;
            if (status != 200) {
                status = put("Intruder detected", msg, NULL, NULL, 0);
            }
        }
    }
    ev->push_ms = (int)((esp_timer_get_time() - t0) / 1000);
    ev->push_result = status == 200 ? PUSH_OK : PUSH_FAILED;
    ESP_LOGI(TAG, "%s: push %s (HTTP %d) in %d ms", name, status == 200 ? "OK" : "FAILED", status, ev->push_ms);
}

static void notify_task(void *arg)
{
    ns_event_t *ev;
    while (true) {
        xQueueReceive(s_queue, &ev, portMAX_DELAY);
        push_event(ev);
        ns_event_release(ev);
    }
}

esp_err_t ns_notify_start(void)
{
    if (!ns_notify_configured()) {
        ESP_LOGW(TAG, "no ntfy topic set (menuconfig -> Night Sentinel network): phone push off");
        return ESP_OK;
    }
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(ns_event_t *));
    // TLS needs a deep stack; 10 KB as in the plan's Day 8 risk note.
    if (xTaskCreatePinnedToCore(notify_task, "notify", 10240, NULL, 3, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "phone push via %s (topic set)", CONFIG_NS_NTFY_SERVER);
    return ESP_OK;
}

void ns_notify_send(ns_event_t *ev)
{
    if (!s_queue) {
        ev->push_result = PUSH_SKIPPED;
        return;
    }
    ns_event_retain(ev);
    if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "push queue full, skipping");
        ev->push_result = PUSH_SKIPPED;
        ns_event_release(ev);
    }
}
