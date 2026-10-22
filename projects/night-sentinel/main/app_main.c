#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "status_led.h"
#include "ir.h"
#include "camera.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

static const char *TAG = "main";

void app_main(void)
{
    status_led_init();
    ir_init();

    // Day 1 IR check: status LED off, IR LEDs held on continuously.
    // status_led_set(true);
    // ir_set(true);
    // ESP_LOGI(TAG, "IR ON (continuous), status LED ON");

    // while (true) {
    //     vTaskDelay(pdMS_TO_TICKS(5000));
    //     ESP_LOGI(TAG, "IR still ON");
    // }

    // Day 2 camera check: initialize camera, then leave IR on continuously.
    esp_err_t err = ns_cam_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "camera init OK");

        int64_t start = esp_timer_get_time(); // in microseconds
        size_t start_free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
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

    
}
