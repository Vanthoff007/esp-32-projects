#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "wifi.h"
#include "wifi_portal.h"

// Source: ESP-IDF v6.1 examples/wifi/getting_started/station, extended.
// Boot: try the saved network. If there isn't one, or it fails, open a setup hotspot and
// wait for a login from the phone page (wifi_portal.c). Once online, reconnect forever.

static const char *TAG = "wifi";

#define GOT_IP_BIT BIT0
#define FAILED_BIT BIT1

#define ATTEMPT_RETRIES   3        // reconnects allowed inside one connection attempt
#define RETRY_MS          30000    // in setup mode, retry known logins this often (nobody on the hotspot)
#define NVS_NAMESPACE     "ns_wifi"

static EventGroupHandle_t s_events;
static volatile bool s_keep_retrying;   // true once online: always reconnect after a drop
static volatile int s_retries;
static volatile int s_last_reason;

// Runs on the system event task, not in app_main, whenever Wi-Fi or IP state changes.
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        s_last_reason = d->reason;
        xEventGroupClearBits(s_events, GOT_IP_BIT);
        if (s_keep_retrying) {
            ESP_LOGW(TAG, "disconnected (reason %d), reconnecting", d->reason);
            esp_wifi_connect();
        } else if (s_retries < ATTEMPT_RETRIES) {
            s_retries++;
            ESP_LOGW(TAG, "disconnected (reason %d), retry %d/%d", d->reason, s_retries, ATTEMPT_RETRIES);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_events, FAILED_BIT);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "a device joined the setup hotspot");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_events, GOT_IP_BIT);
    }
}

static const char *reason_text(int reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
        return "network not found (too far away, or not on 2.4 GHz)";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "wrong password";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "network security not supported (open or WEP networks are refused)";
    default:
        return "connection failed";
    }
}

// The saved login lives in NVS (key-value storage in flash), so it survives reboots and reflashing.
// Only `idf.py erase-flash` clears it.
static bool load_saved(ns_wifi_creds_t *c)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;   // namespace doesn't exist yet: nothing was ever saved
    }
    size_t ssid_size = sizeof(c->ssid);   // in: buffer size, out: bytes read
    size_t pass_size = sizeof(c->pass);
    bool ok = nvs_get_str(h, "ssid", c->ssid, &ssid_size) == ESP_OK &&
              nvs_get_str(h, "pass", c->pass, &pass_size) == ESP_OK;
    nvs_close(h);
    return ok;
}

static void save(const ns_wifi_creds_t *c)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "ssid", c->ssid);
        if (err == ESP_OK) err = nvs_set_str(h, "pass", c->pass);
        if (err == ESP_OK) err = nvs_commit(h);   // nothing reaches flash until commit
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "saving the login failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "saved \"%s\" for next boot", c->ssid);
    }
}

// One connection attempt: up to ATTEMPT_RETRIES reconnects or timeout_ms, whichever ends first.
static bool try_connect(const ns_wifi_creds_t *c, uint32_t timeout_ms)
{
    wifi_config_t cfg = {0};
    // The driver's ssid/password fields are fixed-size byte arrays, not C strings, so copy
    // exactly strlen() bytes. Both lengths are already limited by ns_wifi_creds_t.
    memcpy(cfg.sta.ssid, c->ssid, strlen(c->ssid));
    memcpy(cfg.sta.password, c->pass, strlen(c->pass));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;   // refuse open/WEP/WPA1; WPA3 still allowed
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;           // needed to join WPA3 routers
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    s_retries = 0;
    xEventGroupClearBits(s_events, GOT_IP_BIT | FAILED_BIT);
    ESP_LOGI(TAG, "connecting to \"%s\"", c->ssid);   // never log the password
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP_BIT | FAILED_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (bits & GOT_IP_BIT) {
        return true;
    }
    s_retries = ATTEMPT_RETRIES;   // stop the event handler from reconnecting
    esp_wifi_disconnect();
    ESP_LOGW(TAG, "couldn't join \"%s\": %s (reason %d)", c->ssid, reason_text(s_last_reason), s_last_reason);
    return false;
}

static bool phone_on_hotspot(void)
{
    wifi_sta_list_t list;
    return esp_wifi_ap_get_sta_list(&list) == ESP_OK && list.num > 0;
}

// Setup mode: run the hotspot and page until a login works. Returns once online.
static void run_portal(const ns_wifi_creds_t *saved, bool have_saved, uint32_t timeout_ms)
{
    wifi_config_t ap = {
        .ap = {
            .ssid = CONFIG_NS_PORTAL_SSID,
            .ssid_len = strlen(CONFIG_NS_PORTAL_SSID),
            .password = CONFIG_NS_PORTAL_PASSWORD,
            .channel = 1,
            .max_connection = 2,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    if (strlen(CONFIG_NS_PORTAL_PASSWORD) < 8) {
        ap.ap.authmode = WIFI_AUTH_OPEN;   // WPA2 needs 8+ characters; menuconfig left it short
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));   // hotspot + station at the same time
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ns_portal_start();
    ESP_LOGW(TAG, "SETUP MODE: join Wi-Fi \"%s\" (password \"%s\") and open http://192.168.4.1/",
             CONFIG_NS_PORTAL_SSID, CONFIG_NS_PORTAL_PASSWORD);

    // The last login typed into the page that hasn't worked yet. Kept so it can be retried
    // after the phone leaves: a phone can't run its hotspot while it's on this setup Wi-Fi.
    ns_wifi_creds_t pending = {0};
    bool have_pending = false;

    while (true) {
        ns_wifi_creds_t c;
        if (ns_portal_wait_creds(&c, RETRY_MS)) {
            char msg[160];
            ns_portal_set_busy(true);
            snprintf(msg, sizeof(msg), "Trying \"%s\"...", c.ssid);
            ns_portal_set_status(msg);
            if (try_connect(&c, timeout_ms)) {
                save(&c);   // only save a login that actually worked
                break;
            }
            pending = c;
            have_pending = true;
            snprintf(msg, sizeof(msg), "Couldn't join \"%s\": %s. Retrying every 30 s while nobody is on this Wi-Fi.",
                     c.ssid, reason_text(s_last_reason));
            ns_portal_set_status(msg);
            ns_portal_set_busy(false);
        } else if (!phone_on_hotspot() && (have_pending || have_saved)) {
            // Nobody is using the page. Maybe the phone left to switch its hotspot on, or the
            // router was just off: try the typed login first, then the saved one.
            ns_portal_set_busy(true);
            bool ok = false;
            if (have_pending && try_connect(&pending, timeout_ms)) {
                save(&pending);
                ok = true;
            } else if (have_saved) {
                ok = try_connect(saved, timeout_ms);
            }
            ns_portal_set_busy(false);
            if (ok) {
                break;
            }
        }
    }

    ns_portal_stop();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));   // hotspot off, stay connected
    ESP_LOGI(TAG, "setup mode finished");
}

esp_err_t ns_wifi_connect(uint32_t timeout_ms)
{
    s_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();   // gives the hotspot 192.168.4.1 and a DHCP server

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    // We keep the login in our own NVS namespace, so stop the driver keeping a second copy.
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Login to try first: the one saved from the setup page, else the menuconfig one.
    ns_wifi_creds_t saved = {0};
    bool have_saved = load_saved(&saved);
    if (!have_saved && strlen(CONFIG_NS_WIFI_SSID) > 0) {
        snprintf(saved.ssid, sizeof(saved.ssid), "%s", CONFIG_NS_WIFI_SSID);
        snprintf(saved.pass, sizeof(saved.pass), "%s", CONFIG_NS_WIFI_PASSWORD);
        have_saved = true;
    }

    if (!have_saved || !try_connect(&saved, timeout_ms)) {
        run_portal(&saved, have_saved, timeout_ms);
    }
    s_keep_retrying = true;
    return ESP_OK;
}
