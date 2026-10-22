#pragma once
#include "esp_err.h"
#include "esp_camera.h"

// Start the OV3660: QVGA (320x240), RGB565, 2 frame buffers in PSRAM.
esp_err_t ns_cam_init(void);

// Newest frame, or NULL on failure. Every grab must be paired with ns_cam_release().
camera_fb_t *ns_cam_grab(void);

// Hand the frame buffer back to the driver so it can be refilled.
void ns_cam_release(camera_fb_t *fb);
