#include "driver/gpio.h"
#include "board_pins.h"
#include "ir.h"

// Plain GPIO for now. If dimming is needed later, use LEDC timer 1 / channel 1:
// the camera driver owns timer 0 / channel 0 for XCLK.

static bool s_on;   // remembered here: reading back an output-only pin always gives 0

void ir_init(void)
{
    gpio_reset_pin(PIN_IR_LED);
    gpio_set_direction(PIN_IR_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_IR_LED, 0);
    s_on = false;
}

void ir_set(bool on)
{
    gpio_set_level(PIN_IR_LED, on);
    s_on = on;
}

bool ir_get(void)
{
    return s_on;
}
