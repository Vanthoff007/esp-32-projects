// Speaker playback. Sources: ESP-IDF v6.1 I2S standard-mode API (driver/i2s_std.h) and the
// plan's Day 7 snippet; pins from DFR1154_Examples 5.2 setPins(45, 46, 42).
#include "audio.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

#include "board_pins.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "audio";

#define SAMPLE_RATE 16000
#define CHUNK_SAMPLES 512   // samples scaled and written per I2S call

// The clip is linked into the firmware by EMBED_FILES in CMakeLists.txt. The linker makes
// these two symbols point at its first byte and one past its last byte.
extern const uint8_t warn_pcm_start[] asm("_binary_warn_pcm_start");
extern const uint8_t warn_pcm_end[] asm("_binary_warn_pcm_end");

static i2s_chan_handle_t s_tx = NULL;
static QueueHandle_t s_queue = NULL;
static atomic_bool s_busy = false;
static int s_gain_pct = CONFIG_NS_AUDIO_GAIN_PCT;

void ns_audio_set_gain(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 400) percent = 400;
    s_gain_pct = percent;
}

int ns_audio_get_gain(void)
{
    return s_gain_pct;
}

bool ns_audio_busy(void)
{
    return atomic_load(&s_busy);
}

// Scale samples by the gain and clip them to the 16-bit range, so a loud setting saturates
// instead of wrapping around (which would sound like a crack).
static void write_samples(const int16_t *src, size_t n)
{
    int16_t buf[CHUNK_SAMPLES];
    while (n > 0) {
        size_t k = n < CHUNK_SAMPLES ? n : CHUNK_SAMPLES;
        for (size_t i = 0; i < k; i++) {
            int32_t v = (int32_t)src[i] * s_gain_pct / 100;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            buf[i] = (int16_t)v;
        }
        size_t written = 0;
        i2s_channel_write(s_tx, buf, k * sizeof(int16_t), &written, portMAX_DELAY);
        src += k;
        n -= k;
    }
}

static void play_beep(int ms, int hz)
{
    static int16_t tone[SAMPLE_RATE / 10];   // 100 ms of tone, played as often as needed
    static bool made = false;
    if (!made) {
        for (int i = 0; i < SAMPLE_RATE / 10; i++) {
            tone[i] = (int16_t)(8000 * sinf(2.0f * (float)M_PI * hz * i / SAMPLE_RATE));
        }
        made = true;
    }
    for (int done = 0; done < ms; done += 100) {
        write_samples(tone, SAMPLE_RATE / 10);
    }
}

static void play_silence(int ms)
{
    static const int16_t zeros[CHUNK_SAMPLES] = {0};
    int n = SAMPLE_RATE * ms / 1000;
    while (n > 0) {
        int k = n < CHUNK_SAMPLES ? n : CHUNK_SAMPLES;
        write_samples(zeros, k);
        n -= k;
    }
}

static void audio_task(void *arg)
{
    ns_clip_t clip;
    while (true) {
        xQueueReceive(s_queue, &clip, portMAX_DELAY);
        int64_t t0 = esp_timer_get_time();
        play_beep(300, 1000);   // 1 kHz beep (1000 is also the tone's frequency used to build it)
        if (clip == CLIP_WARN) {
            play_silence(150);
            const int16_t *pcm = (const int16_t *)warn_pcm_start;
            size_t n = (warn_pcm_end - warn_pcm_start) / sizeof(int16_t);
            write_samples(pcm, n);
        }
        play_silence(200);   // push the last samples out of the DMA buffers
        ESP_LOGI(TAG, "played clip %d in %lld ms", clip, (esp_timer_get_time() - t0) / 1000);
        atomic_store(&s_busy, false);
    }
}

esp_err_t ns_audio_init(void)
{
#if !CONFIG_NS_AUDIO_ENABLED
    ESP_LOGW(TAG, "audio disabled in menuconfig (Night Sentinel alerts)");
    return ESP_OK;
#else
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear = true;   // send zeros (silence) whenever there's nothing to play
    esp_err_t err = i2s_new_channel(&cc, &s_tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
        return err;
    }
    i2s_std_config_t sc = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,   // the MAX98357 makes its own clock from BCLK
            .bclk = PIN_SPK_BCLK,
            .ws = PIN_SPK_WS,
            .dout = PIN_SPK_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {0},
        },
    };
    // Send each mono sample on both the left and right slot: the amplifier plays (L+R)/2 by
    // default, so a left-only signal would be half as loud.
    sc.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    err = i2s_channel_init_std_mode(s_tx, &sc);
    if (err == ESP_OK) err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S setup: %s", esp_err_to_name(err));
        return err;
    }
    s_queue = xQueueCreate(1, sizeof(ns_clip_t));
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 4, NULL, 0);
    ESP_LOGI(TAG, "speaker ready: clip %.2f s, gain %d %%",
             (warn_pcm_end - warn_pcm_start) / 2.0f / SAMPLE_RATE, s_gain_pct);
    return ESP_OK;
#endif
}

bool ns_audio_play(ns_clip_t clip)
{
    if (!s_queue) {
        return false;   // disabled or init failed
    }
    bool expected = false;
    // Claim "busy" only if it was free: compare-and-swap, so two callers can't both win.
    if (!atomic_compare_exchange_strong(&s_busy, &expected, true)) {
        return false;
    }
    if (xQueueSend(s_queue, &clip, 0) != pdTRUE) {
        atomic_store(&s_busy, false);
        return false;
    }
    return true;
}
