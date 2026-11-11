#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "status_led.h"

static volatile ns_led_mode_t s_mode = NS_LED_IDLE;
static bool s_started = false;

void status_led_init(void)
{
    gpio_reset_pin(PIN_STATUS_LED);
    gpio_set_direction(PIN_STATUS_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_STATUS_LED, 0);
}

void status_led_set(bool on)
{
    gpio_set_level(PIN_STATUS_LED, on);
}

static void led_task(void *arg)
{
    bool on = false;
    int tick = 0;
    while (true) {
        switch (s_mode) {
        case NS_LED_ACTIVE: on = true; break;
        case NS_LED_ERROR: on = !on; break;               // toggles every 100 ms = 5 Hz
        case NS_LED_IDLE: on = (tick % 20) == 0; break;   // 100 ms flash every 2 s
        }
        status_led_set(on);
        tick++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void ns_led_start(void)
{
    if (s_started) return;
    s_started = true;
    xTaskCreatePinnedToCore(led_task, "led", 2048, NULL, 1, NULL, 0);
}

void ns_led_set_mode(ns_led_mode_t mode)
{
    s_mode = mode;
}
