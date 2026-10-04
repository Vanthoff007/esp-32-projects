#include "driver/gpio.h"
#include "board_pins.h"
#include "status_led.h"

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
