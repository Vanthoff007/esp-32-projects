// Internet time. Source: ESP-IDF v6.1 examples/protocols/sntp (esp_netif_sntp API).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "ns_time.h"
#include "sdkconfig.h"

static const char *TAG = "time";

static void on_sync(struct timeval *tv)
{
    char buf[40];
    ns_time_iso(tv->tv_sec, buf, sizeof(buf));
    ESP_LOGI(TAG, "clock set from the internet: %s", buf);
}

void ns_time_start(void)
{
    setenv("TZ", CONFIG_NS_TZ, 1);   // localtime() reads this
    tzset();
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = on_sync;
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SNTP start failed: %s", esp_err_to_name(err));
    }
}

bool ns_time_synced(void)
{
    return time(NULL) > 1700000000;   // any date after Nov 2023 means it was set
}

void ns_time_iso(time_t t, char *buf, size_t len)
{
    if (t < 1700000000) {
        snprintf(buf, len, "uptime %.1f s", esp_timer_get_time() / 1e6);
        return;
    }
    struct tm tm;
    localtime_r(&t, &tm);
    char tz[8];
    strftime(tz, sizeof(tz), "%z", &tm);   // "+0530"
    size_t n = strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &tm);
    // ISO 8601 wants "+05:30", strftime gives "+0530".
    if (n + 7 <= len && strlen(tz) == 5) {
        snprintf(buf + n, len - n, "%.3s:%.2s", tz, tz + 3);
    }
}
