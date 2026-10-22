#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "status_led.h"
#include "ir.h"
#include "camera.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "ns_mdns.h"
#include "debug_http.h"

// Set to 1 to re-run the Day 2 capture test (1000 frames) at boot. Watch the log for the
// driver's "cam_hal: FB-SIZE" lines too: those are short frames it silently dropped.
#define RUN_CAPTURE_TEST 0

static const char *TAG = "main";

#if RUN_CAPTURE_TEST
// Day 2 Step 3: grab and release 1000 frames, report FPS, NULL frames and PSRAM.
static void run_capture_test(void)
{
    size_t start_free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    int64_t start = esp_timer_get_time(); // in microseconds

    int counter = 0;
    for (int i = 0; i < 1000; i++) {
        camera_fb_t *fb = ns_cam_grab();
        if (fb == NULL) {
            counter++;
        }
        else {
            ns_cam_release(fb);
        }
    }

    int64_t end = esp_timer_get_time(); // in microseconds
    size_t end_free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    double fps = 1000.0 / ((end - start) / 1000000.0);
    ESP_LOGI(TAG, "grabbed 1000 frames in %lld ms, %d failures, %d fps, free memory: %zu -> %zu", (end - start) / 1000, counter, (int)fps, start_free_size, end_free_size);
}
#endif

void app_main(void)
{
    status_led_init();
    ir_init();

    // NVS (a small key-value store in flash) must be ready before Wi-Fi starts:
    // the Wi-Fi driver keeps its calibration data there.
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    // Day 1 IR check: status LED off, IR LEDs held on continuously.
    // status_led_set(true);
    // ir_set(true);
    // ESP_LOGI(TAG, "IR ON (continuous), status LED ON");

    // while (true) {
    //     vTaskDelay(pdMS_TO_TICKS(5000));
    //     ESP_LOGI(TAG, "IR still ON");
    // }

    esp_err_t err = ns_cam_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "camera init OK");

#if RUN_CAPTURE_TEST
    run_capture_test();
#endif

    // Day 2 Steps 4-5: join Wi-Fi, then serve the live view and snapshots.
    if (ns_wifi_connect(15000) == ESP_OK) {
        ESP_LOGI(TAG, "wifi OK");
        ns_mdns_start("night-sentinel");   // http://night-sentinel.local/
        ns_debug_http_start();
        status_led_set(true);   // LED on = online, open http://<IP>/ in a browser
    }
}
