// Night Sentinel: start-up order and wiring only. What the project does lives in three layers:
//
//   platform/   reusable board firmware: camera + frame hub, SD card, Wi-Fi, web server,
//               recorder, inference engine. Knows nothing about people or alarms.
//   models/     one folder per model; menuconfig -> Platform: inference picks the one built in.
//   app/        Night Sentinel itself: person boxes -> intrusion state machine -> speaker,
//               phone push, SD evidence, video clip.
//
// Tasks (who runs where):
//   capture  core 1, prio 5   grabs frames, runs the model, calls the app with the results
//   jpeg     core 0           encodes frames for /snap.jpg, the live stream and the recorder
//   recorder core 0           writes video files to the SD card
//   storage  core 0           all other SD card writes
//   audio    core 0           plays the warning clip
//   notify   core 0           HTTPS push to the phone
//   monitor  core 0, prio 2   day/night switch every 2 s, heartbeat every 60 s
//   led      core 0, prio 1   status LED pattern
#include <stdlib.h>
#include <time.h>

#include "audio.h"
#include "camera.h"
#include "debug_http.h"
#include "esp_log.h"
#include "frame_hub.h"
#include "infer.h"
#include "ir.h"
#include "light.h"
#include "notify.h"
#include "ns_mdns.h"
#include "ns_time.h"
#include "nvs_flash.h"
#include "recorder.h"
#include "sdkconfig.h"
#include "sentinel.h"
#include "status_led.h"
#include "storage.h"
#include "wifi.h"

static const char *TAG = "main";

void app_main(void)
{
    // ---- platform: board ----
    status_led_init();
    ir_init();
    ns_led_start();
    // Time zone first: after a quick reset the clock is still right (it survives in RTC
    // memory), and events in the first seconds, before Wi-Fi, must not be stamped in UTC.
    setenv("TZ", CONFIG_NS_TZ, 1);
    tzset();

    // NVS (a small key-value store in flash): Wi-Fi calibration, saved login, event counter.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    if (ns_cam_init() != ESP_OK) {
        ESP_LOGE(TAG, "camera failed: nothing can work without it. Fast-blinking LED.");
        ns_led_set_mode(NS_LED_ERROR);
        return;
    }
    ns_light_init();      // on failure the day/night switch falls back to the camera's gain

    // ---- platform: services (none of these fail for a missing card, speaker or network) ----
    ns_storage_start();   // SD card: also where the model's weights file is read from
    ns_audio_init();
    ns_notify_start();    // waits for Wi-Fi per push
    ns_rec_start();       // video recorder (off unless switched on)

    // ---- app, then the model, then the camera loop that feeds them ----
    sentinel_start();     // listens for results; adds its page and test actions to the web server
    ns_infer_start();     // the model chosen in menuconfig; loaded by the capture task
    ns_hub_start();       // from here on frames flow: before Wi-Fi, so the alarm works offline

    // Join Wi-Fi. Blocks until online (opening the setup hotspot if needed), then reconnects
    // by itself after any drop. Detection keeps running the whole time.
    if (ns_wifi_connect(15000) == ESP_OK) {
        ns_time_start();
        ns_mdns_start("night-sentinel");   // http://night-sentinel.local/
#if CONFIG_NS_DEBUG_HTTP
        ns_debug_http_start();
#endif
        ESP_LOGI(TAG, "online");
    }
}
