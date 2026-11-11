// microSD card. Sources: ESP-IDF v6.1 examples/storage/sd_card/sdspi (SPI bus +
// esp_vfs_fat_sdspi_mount); pins from DFR1154_Examples 6.9 / 6.10.
#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

#define MOUNT NS_STORAGE_MOUNT
#define QUEUE_LEN 16
#define REMOUNT_EVERY_MS 30000   // with no card, try again at most this often

typedef enum { MSG_JOB, MSG_LINE } msg_kind_t;

typedef struct {
    msg_kind_t kind;
    ns_storage_job_t job;   // MSG_JOB
    void *arg;
    char file[24];          // MSG_LINE
    const char *header;
    char *line;             // malloc'd, freed by the task
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
        .format_if_mount_failed = false,   // never wipe a card that has data on it
        .max_files = 8,                    // app files + a recording + a web download at once
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdspi_mount(MOUNT, &host, &slot, &mc, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        return err;
    }
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

// fopen that creates the file's folder when it is missing (one level, e.g. "ev").
static FILE *open_making_dir(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    if (f) return f;
    char dir[96];
    strlcpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (!slash || slash - dir <= (int)strlen(MOUNT)) return NULL;   // file is in the card's root
    *slash = '\0';
    mkdir(dir, 0775);
    return fopen(path, mode);
}

bool ns_storage_write_file(const char *rel_path, const void *data, size_t len, int *write_ms)
{
    if (write_ms) *write_ms = -1;
    if (!data || !ensure_mounted()) return false;

    char path[128];
    snprintf(path, sizeof(path), MOUNT "/%s", rel_path);
    int64_t t0 = esp_timer_get_time();
    bool ok = false;
    FILE *f = open_making_dir(path, "wb");
    if (f) {
        ok = fwrite(data, 1, len, f) == len;
        // fflush + fsync make sure the data is really on the card before we report success.
        ok = fflush(f) == 0 && fsync(fileno(f)) == 0 && ok;
        ok = fclose(f) == 0 && ok;
    }
    int ms = (int)((esp_timer_get_time() - t0) / 1000);
    if (write_ms) *write_ms = ms;
    s_writes++;
    if (ok) {
        s_last_write_ms = ms;
        s_write_ms[s_write_n % STATS_N] = ms;
        s_write_n++;
    } else {
        s_failures++;
        ESP_LOGW(TAG, "%s: write failed (errno %d)", rel_path, errno);
        unmount_after_error();
    }
    return ok;
}

bool ns_storage_append_now(const char *rel_path, const char *header, const char *line)
{
    if (!ensure_mounted()) return false;
    char path[128];
    snprintf(path, sizeof(path), MOUNT "/%s", rel_path);
    struct stat st;
    bool is_new = stat(path, &st) != 0;
    bool ok = false;
    FILE *f = open_making_dir(path, "a");
    if (f) {
        ok = true;
        if (is_new && header) ok = fprintf(f, "%s\n", header) > 0;
        ok = ok && fprintf(f, "%s\n", line) > 0;
        // A power cut loses at most the line being written.
        ok = ok && fflush(f) == 0 && fsync(fileno(f)) == 0;
        ok = (fclose(f) == 0) && ok;
    }
    if (!ok) unmount_after_error();
    return ok;
}

static void storage_task(void *arg)
{
    msg_t m;
    while (true) {
        xQueueReceive(s_queue, &m, portMAX_DELAY);
        if (m.kind == MSG_JOB) {
            m.job(m.arg);
        } else {
            ns_storage_append_now(m.file, m.header, m.line);
            free(m.line);
        }
    }
}

esp_err_t ns_storage_start(void)
{
    esp_err_t err = mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no SD card (%s): nothing will be saved until one is inserted",
                 esp_err_to_name(err));
    }
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(msg_t));
    if (xTaskCreatePinnedToCore(storage_task, "storage", 4096, NULL, 2, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool ns_storage_run(ns_storage_job_t job, void *arg)
{
    if (!s_queue) return false;
    msg_t m = {.kind = MSG_JOB, .job = job, .arg = arg};
    return xQueueSend(s_queue, &m, 0) == pdTRUE;
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
