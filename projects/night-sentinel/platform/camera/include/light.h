#pragma once
#include "esp_err.h"

// LTR-308 ambient light sensor. It sits on the camera's I2C (SCCB) bus at 0x53, so call
// ns_light_init() after ns_cam_init(). The I2C driver queues the two devices' transfers,
// so it can be read from any task while the camera runs.
// Its 940 nm response is small, so the board's own IR LEDs barely change the reading
// (unlike frame brightness, which jumps when IR comes on).

esp_err_t ns_light_init(void);

// Latest reading in lux (0.6 * counts / (gain * integration factor), from the datasheet).
// Returns an error if the sensor isn't initialised or the I2C read fails.
esp_err_t ns_light_read_lux(float *lux);
