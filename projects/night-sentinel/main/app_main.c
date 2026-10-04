#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "status_led.h"
#include "ir.h"

static const char *TAG = "main";

void app_main(void)
{
    status_led_init();
    ir_init();

    // Day 1 IR check: status LED off, IR LEDs held on continuously.
    status_led_set(true);
    ir_set(true);
    ESP_LOGI(TAG, "IR ON (continuous), status LED ON");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "IR still ON");
    }
}
