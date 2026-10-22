#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "wifi.h"

// Source: ESP-IDF v6.1 examples/wifi/getting_started/station, trimmed.
// Unlike the example, it never gives up: every disconnect triggers a reconnect.

static const char *TAG = "wifi";

#define GOT_IP_BIT BIT0

static EventGroupHandle_t s_wifi_events;

// Runs on the system event task, not in app_main, whenever Wi-Fi or IP state changes.
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        // Common reasons: 201 = network not found, 15 / 204 = wrong password.
        ESP_LOGW(TAG, "disconnected (reason %d), reconnecting", d->reason);
        xEventGroupClearBits(s_wifi_events, GOT_IP_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, GOT_IP_BIT);
    }
}

esp_err_t ns_wifi_connect(uint32_t timeout_ms)
{
    if (strlen(CONFIG_NS_WIFI_SSID) == 0) {
        ESP_LOGE(TAG, "no SSID: set it in idf.py menuconfig -> Night Sentinel Wi-Fi");
        return ESP_ERR_INVALID_STATE;
    }

    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        on_event, NULL, NULL));

    wifi_config_t cfg = {
        .sta = {
            .ssid = CONFIG_NS_WIFI_SSID,
            .password = CONFIG_NS_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,   // refuse open/WEP/WPA1 networks
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());   // triggers WIFI_EVENT_STA_START -> esp_wifi_connect()

    ESP_LOGI(TAG, "connecting to \"%s\"", CONFIG_NS_WIFI_SSID);   // never log the password

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, GOT_IP_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (!(bits & GOT_IP_BIT)) {
        ESP_LOGE(TAG, "no IP after %lu ms (still retrying in the background)",
                 (unsigned long)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
