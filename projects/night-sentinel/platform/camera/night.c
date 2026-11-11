#include "night.h"

#include "camera.h"
#include "esp_log.h"
#include "ir.h"
#include "light.h"
#include "sdkconfig.h"

static const char *TAG = "night";

static volatile float s_lux = -1;
static volatile ns_ir_mode_t s_mode =
#if CONFIG_NS_IR_ALWAYS_ON
    NS_IR_MODE_ON;
#elif CONFIG_NS_IR_ALWAYS_OFF
    NS_IR_MODE_OFF;
#else
    NS_IR_MODE_AUTO;
#endif

void ns_night_set_mode(ns_ir_mode_t mode)
{
    s_mode = mode;
}

ns_ir_mode_t ns_night_get_mode(void)
{
    return s_mode;
}

float ns_night_lux(void)
{
    return s_lux;
}

static void set_ir(bool on)
{
    if (on == ir_get()) return;
    ir_set(on);
    // Night mode (AEC2) lets the sensor stretch exposure up to 180 ms: the Day 3 sweep's best
    // setting in the dark, but it would cost frame rate in daylight, so it follows the IR.
    sensor_t *s = esp_camera_sensor_get();
    if (s) s->set_aec2(s, on ? 1 : 0);
    ESP_LOGI(TAG, "%s: IR %s (lux %.1f)", on ? "night mode" : "day mode", on ? "ON" : "off", s_lux);
}

void ns_night_poll(void)
{
    float lux;
    s_lux = ns_light_read_lux(&lux) == ESP_OK ? lux : -1;

    switch (s_mode) {
    case NS_IR_MODE_ON: set_ir(true); return;
    case NS_IR_MODE_OFF: set_ir(false); return;
    case NS_IR_MODE_AUTO: break;
    }
    if (s_lux >= 0) {
        if (!ir_get() && s_lux < CONFIG_NS_IR_ON_BELOW_LUX) set_ir(true);
        else if (ir_get() && s_lux > CONFIG_NS_IR_OFF_ABOVE_LUX) set_ir(false);
    } else {
        // No light sensor: use the sensor's own gain. In the dark auto-exposure runs it up to
        // the 16x ceiling; in room light it sits near 2-7x.
        float gain;
        int lines;
        ns_cam_read_ae(&gain, &lines);
        if (!ir_get() && gain >= 15.5f) set_ir(true);
        else if (ir_get() && gain <= 4.0f) set_ir(false);
    }
}
