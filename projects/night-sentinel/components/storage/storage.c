// microSD evidence. Sources: plan Day 9 snippet; ESP-IDF v6.1 examples/storage/sd_card/sdspi
// (SPI bus + esp_vfs_fat_sdspi_mount); pins from DFR1154_Examples 6.9 / 6.10.
#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "board_pins.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

static const char *TAG = "storage";

#define MOUNT "/sdcard"
#define QUEUE_LEN 16
#define PUSH_WAIT_MS 25000
#define REMOUNT_EVERY_MS 30000   // with no card, try again at most this often

#define EVENTS_HEADER "event_id,time,uptime_s,score,x1,y1,x2,y2,ir,lux,test,jpeg_file,jpeg_bytes,jpeg_ok,write_ms,push_ok,push_ms"

typedef enum { MSG_EVENT, MSG_LINE } msg_kind_t;

typedef struct {
    msg_kind_t kind;
    ns_event_t *ev;    // MSG_EVENT
    char file[24];     // MSG_LINE
    const char *header;
    char *line;        // malloc'd, freed by the task
} msg_t;

static QueueHandle_t s_queue = NULL;
static sdmmc_card_t *s_card = NULL;
static bool s_bus_ready = false;
static volatile bool s_mounted = false;
static int64_t s_last_mount_try_us = 0;

// Write-time statistics.
#define STATS_N 32
static int s_write_ms[STATS_N];
static int s_write_n = 0;
static volatile int s_last_write_ms = -1;
static volatile uint32_t s_writes = 0, s_failures = 0;

bool ns_storage_mounted(void)
{
    return s_mounted;
}

static esp_err_t mount(void)
{
    s_last_mount_try_us = esp_timer_get_time();
    if (!s_bus_ready) {
        spi_bus_config_t bus = {
            .mosi_io_num = PIN_SD_MOSI,
            .miso_io_num = PIN_SD_MISO,
            .sclk_io_num = PIN_SD_SCK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };
        esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SDSPI_DEFAULT_DMA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPI bus: %s", esp_err_to_name(err));
            return err;
        }
        s_bus_ready = true;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = SPI2_HOST;
    esp_vfs_fat_sdmmc_mount_config_t mc = {
        .format_if_mount_failed = false,   // never wipe a card that has evidence on it
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdspi_mount(MOUNT, &host, &slot, &mc, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        return err;
    }
    mkdir(MOUNT "/ev", 0775);   // fails harmlessly if it exists
    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted: %s, %llu MB", s_card->cid.name,
             (unsigned long long)s_card->csd.capacity * s_card->csd.sector_size / (1024 * 1024));
    return ESP_OK;
}

static void unmount_after_error(void)
{
    if (s_card) {
        esp_vfs_fat_sdcard_unmount(MOUNT, s_card);
        s_card = NULL;
    }
    s_mounted = false;
    ESP_LOGW(TAG, "SD card error: unmounted, will retry");
}

static bool ensure_mounted(void)
{
    if (s_mounted) return true;
    if (esp_timer_get_time() - s_last_mount_try_us < REMOUNT_EVERY_MS * 1000LL) return false;
    return mount() == ESP_OK;
}

void ns_storage_space(uint32_t *total_mb, uint32_t *free_mb)
{
    *total_mb = *free_mb = 0;
    if (!s_mounted) return;
    uint64_t total = 0, free_b = 0;
    if (esp_vfs_fat_info(MOUNT, &total, &free_b) == ESP_OK) {
        *total_mb = (uint32_t)(total / (1024 * 1024));
        *free_mb = (uint32_t)(free_b / (1024 * 1024));
    }
}

static int cmp_int(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

void ns_storage_write_stats(int *last_ms, int *median_ms, uint32_t *writes, uint32_t *failures)
{
    int n = s_write_n < STATS_N ? s_write_n : STATS_N;
    int sorted[STATS_N];
    memcpy(sorted, s_write_ms, sizeof(int) * n);
    qsort(sorted, n, sizeof(int), cmp_int);
    *last_ms = s_last_write_ms;
    *median_ms = n ? sorted[n / 2] : -1;
    *writes = s_writes;
    *failures = s_failures;
}

// Append a line to a file, writing the header first if the file is new. fflush + fsync make
// sure the line is really on the card, so a power cut loses at most the line being written.
static bool append_line(const char *path, const char *header, const char *line)
{
    struct stat st;
    bool is_new = stat(path, &st) != 0;
    FILE *f = fopen(path, "a");
    if (!f) return false;
    bool ok = true;
    if (is_new && header) ok = fprintf(f, "%s\n", header) > 0;
    ok = ok && fprintf(f, "%s\n", line) > 0;
    ok = ok && fflush(f) == 0 && fsync(fileno(f)) == 0;
    ok = (fclose(f) == 0) && ok;
    return ok;
}

static void save_event(ns_event_t *ev)
{
    char name[12], file[64] = "", path[96];
    ns_event_name(ev, name, sizeof(name));
    if (ev->wall) {
        struct tm tm;
        localtime_r(&ev->wall, &tm);
        char stamp[20];
        strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
        snprintf(file, sizeof(file), "ev/%s_%s.jpg", name, stamp);
    } else {
        snprintf(file, sizeof(file), "ev/%s_up%lld.jpg", name, ev->uptime_ms / 1000);
    }

    bool jpeg_ok = false;
    int write_ms = -1;
    if (ev->jpg && ensure_mounted()) {
        snprintf(path, sizeof(path), MOUNT "/%s", file);
        int64_t t0 = esp_timer_get_time();
        FILE *f = fopen(path, "wb");
        if (f) {
            jpeg_ok = fwrite(ev->jpg, 1, ev->jpg_len, f) == ev->jpg_len;
            jpeg_ok = fflush(f) == 0 && fsync(fileno(f)) == 0 && jpeg_ok;
            jpeg_ok = fclose(f) == 0 && jpeg_ok;
        }
        write_ms = (int)((esp_timer_get_time() - t0) / 1000);
        s_writes++;
        if (jpeg_ok) {
            s_last_write_ms = write_ms;
            s_write_ms[s_write_n % STATS_N] = write_ms;
            s_write_n++;
        } else {
            s_failures++;
            ESP_LOGW(TAG, "%s: JPEG write failed (errno %d)", name, errno);
            unmount_after_error();
        }
    }

    // Wait for the phone push to finish, so the row can record it.
    int64_t t_wait = esp_timer_get_time();
    while (ev->push_result == PUSH_PENDING && esp_timer_get_time() - t_wait < PUSH_WAIT_MS * 1000LL) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    char when[40], line[320];
    if (ev->wall) {
        struct tm tm;
        localtime_r(&ev->wall, &tm);
        char tz[8];
        strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%S", &tm);
        strftime(tz, sizeof(tz), "%z", &tm);
        size_t n = strlen(when);
        snprintf(when + n, sizeof(when) - n, "%.3s:%.2s", tz, tz + 3);
    } else {
        when[0] = '\0';
    }
    snprintf(line, sizeof(line), "%s,%s,%.1f,%.2f,%d,%d,%d,%d,%d,%.1f,%d,%s,%u,%d,%d,%d,%d",
             name, when, ev->uptime_ms / 1000.0, ev->score, ev->box[0], ev->box[1], ev->box[2],
             ev->box[3], ev->ir, ev->lux, ev->test, jpeg_ok ? file : "", (unsigned)ev->jpg_len,
             jpeg_ok, write_ms, ev->push_result == PUSH_OK, ev->push_ms);
    bool row_ok = s_mounted && append_line(MOUNT "/events.csv", EVENTS_HEADER, line);
    if (!row_ok && s_mounted) unmount_after_error();
    ESP_LOGI(TAG, "%s: jpeg %s (%d ms), csv row %s", name, jpeg_ok ? "saved" : "NOT saved",
             write_ms, row_ok ? "saved" : "NOT saved");
}

static void storage_task(void *arg)
{
    msg_t m;
    while (true) {
        xQueueReceive(s_queue, &m, portMAX_DELAY);
        if (m.kind == MSG_EVENT) {
            save_event(m.ev);
            ns_event_release(m.ev);
        } else {
            if (ensure_mounted()) {
                char path[48];
                snprintf(path, sizeof(path), MOUNT "/%s", m.file);
                if (!append_line(path, m.header, m.line)) unmount_after_error();
            }
            free(m.line);
        }
    }
}

esp_err_t ns_storage_start(void)
{
    esp_err_t err = mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no SD card (%s): evidence won't be saved until one is inserted",
                 esp_err_to_name(err));
    }
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(msg_t));
    if (xTaskCreatePinnedToCore(storage_task, "storage", 4096, NULL, 2, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ns_storage_save_event(ns_event_t *ev)
{
    if (!s_queue) return;
    ns_event_retain(ev);
    msg_t m = {.kind = MSG_EVENT, .ev = ev};
    if (xQueueSend(s_queue, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "storage queue full, event not saved");
        ns_event_release(ev);
    }
}

void ns_storage_append(const char *file, const char *header, const char *line)
{
    if (!s_queue) return;
    msg_t m = {.kind = MSG_LINE, .header = header, .line = strdup(line)};
    if (!m.line) return;
    strncpy(m.file, file, sizeof(m.file) - 1);
    if (xQueueSend(s_queue, &m, 0) != pdTRUE) {
        free(m.line);
    }
}
