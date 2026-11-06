#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "status_led.h"
#include "ir.h"
#include "camera.h"
#include "light.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "ns_mdns.h"
#include "debug_http.h"

// Set to 1 to re-run the Day 2 capture test (1000 frames) at boot. Watch the log for the
// driver's "cam_hal: FB-SIZE" lines too: those are short frames it silently dropped.
#define RUN_CAPTURE_TEST 0

// Log lux, frame brightness, sensor gain and exposure this often (0 = off). Day 3 Step 4.
#define LOG_LIGHT_EVERY_MS 5000

// Set to 1 to re-run the shared-I2C-bus test at boot (60 s, before Wi-Fi). Day 3 Step 4.
#define RUN_LIGHT_BUS_TEST 0

static const char *TAG = "main";

#if RUN_LIGHT_BUS_TEST
// The LTR-308 shares the camera's I2C bus. This checks that reading it never disturbs the
// camera: 30 s camera only (control), then 30 s camera + I2C reads 10x a second.
// Compare fps, NULL frames and the driver's "FB-SIZE" lines between the two halves.

static volatile bool s_grabbing;
static volatile int s_frames;
static volatile int s_null_frames;

// Grabs frames as fast as the camera delivers them, like the detector loop will.
static void grab_task(void *arg)
{
    while (s_grabbing) {
        camera_fb_t *fb = ns_cam_grab();
        if (fb == NULL) {
            s_null_frames++;
        } else {
            s_frames++;
            ns_cam_release(fb);
        }
    }
    vTaskDelete(NULL);   // a FreeRTOS task must delete itself instead of returning
}

static void bus_test_phase(const char *name, bool use_bus, int seconds)
{
    s_frames = 0;
    s_null_frames = 0;
    int lux_ok = 0, lux_err = 0, sccb_err = 0;
    float lux_min = 1e9f, lux_max = -1;
    sensor_t *s = esp_camera_sensor_get();

    ESP_LOGI(TAG, "bus test: phase \"%s\" start (%d s)", name, seconds);
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < seconds * 10; i++) {
        if (use_bus) {
            float lux;
            if (ns_light_read_lux(&lux) == ESP_OK) {
                lux_ok++;
                if (lux < lux_min) lux_min = lux;
                if (lux > lux_max) lux_max = lux;
            } else {
                lux_err++;
            }
            // Camera register reads over the same bus: the chip ID must always read 0x36 0x60.
            if (s->get_reg(s, 0x300a, 0xff) != 0x36 || s->get_reg(s, 0x300b, 0xff) != 0x60) {
                sccb_err++;
            }
            float gain;
            int lines;
            ns_cam_read_ae(&gain, &lines);   // what /status does: 5 more register reads
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    float secs = (esp_timer_get_time() - t0) / 1e6f;
    ESP_LOGI(TAG, "bus test: phase \"%s\" end: %d frames in %.1f s = %.1f fps, %d NULL",
             name, s_frames, secs, s_frames / secs, s_null_frames);
    if (use_bus) {
        ESP_LOGI(TAG, "bus test: LTR-308 %d ok / %d errors (%.1f..%.1f lux), camera chip-ID mismatches %d",
                 lux_ok, lux_err, lux_ok ? lux_min : 0, lux_ok ? lux_max : 0, sccb_err);
    }
}

static void run_light_bus_test(void)
{
    s_grabbing = true;
    xTaskCreate(grab_task, "grab_test", 4096, NULL, 5, NULL);
    // Warm-up first: right after init, auto-exposure is still settling and the frame rate is
    // lower (first run 2026-11-06: 11.3 fps in the first 30 s, 15.8 fps after). Then a
    // control phase on each side of the I2C phase.
    bus_test_phase("warm-up", false, 10);
    bus_test_phase("camera only", false, 20);
    bus_test_phase("camera + I2C", true, 30);
    bus_test_phase("camera only again", false, 20);
    s_grabbing = false;
    vTaskDelay(pdMS_TO_TICKS(200));   // let grab_task finish its last frame
}
#endif

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

    // Day 3 Step 4: list the camera bus, then start the LTR-308 light sensor on it.
    ns_cam_scan_bus();
    ns_light_init();   // on failure, lux reads report errors and the log shows "lux n/a"

#if RUN_CAPTURE_TEST
    run_capture_test();
#endif
#if RUN_LIGHT_BUS_TEST
    run_light_bus_test();
#endif

    // Join Wi-Fi (opens the setup hotspot if needed; blocks until online),
    // then serve the live view, snapshots and camera controls.
    if (ns_wifi_connect(15000) == ESP_OK) {
        ESP_LOGI(TAG, "wifi OK");
        ns_mdns_start("night-sentinel");   // http://night-sentinel.local/
        ns_debug_http_start();
        status_led_set(true);   // LED on = online, open http://<IP>/ in a browser
    }

#if LOG_LIGHT_EVERY_MS
    // Day 3 Step 4: candidate day/night signals, to compare with a phone lux reading.
    while (true) {
        camera_fb_t *fb = ns_cam_grab();
        if (fb != NULL) {
            float luma = ns_cam_mean_luma(fb);
            ns_cam_release(fb);
            float gain;
            int lines;
            ns_cam_read_ae(&gain, &lines);
            float lux = -1;
            ns_light_read_lux(&lux);   // stays -1 if the sensor isn't working
            ESP_LOGI(TAG, "light: lux %.1f, luma %.1f, gain %.2fx, exposure %.1f ms, IR %s",
                     lux, luma, gain, lines * NS_CAM_LINE_US / 1000.0f, ir_get() ? "on" : "off");
        }
        vTaskDelay(pdMS_TO_TICKS(LOG_LIGHT_EVERY_MS));
    }
#endif
}
