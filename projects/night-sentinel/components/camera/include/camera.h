#pragma once
#include "esp_err.h"
#include "esp_camera.h"

// One sensor line lasts 2300 cycles at SYSCLK 30 MHz (see set_pll in camera.c).
// Exposure is counted in lines, so lines * NS_CAM_LINE_US = exposure time in microseconds.
#define NS_CAM_LINE_US 76.7f

// Start the OV3660: QVGA (320x240), RGB565, 2 frame buffers in PSRAM.
esp_err_t ns_cam_init(void);

// Newest frame, or NULL on failure. Every grab must be paired with ns_cam_release().
camera_fb_t *ns_cam_grab(void);

// Hand the frame buffer back to the driver so it can be refilled.
void ns_cam_release(camera_fb_t *fb);

// Average brightness of an RGB565 frame: 0 = black, 255 = white. Samples every 4th pixel.
// Returns -1 for any other pixel format.
float ns_cam_mean_luma(const camera_fb_t *fb);

// What auto-exposure is doing right now, read from the sensor:
// *gain = analog gain (1.0 = none), *exposure_lines = exposure time in lines.
void ns_cam_read_ae(float *gain, int *exposure_lines);

// Highest gain auto-exposure may use, as a multiplier (1 to 63). The driver's
// set_gainceiling() writes the wrong units on the OV3660, so use these instead.
esp_err_t ns_cam_set_gain_ceiling(int times);
int ns_cam_get_gain_ceiling(void);

// Log every device that answers on the camera's I2C (SCCB) bus, and check for the
// LTR-308 light sensor. Call once, after ns_cam_init().
void ns_cam_scan_bus(void);
