#include "driver/i2c_master.h"
#include "esp_log.h"
#include "light.h"

// Register map and lux formula: LTR-308ALS-01 datasheet (Lite-On).

static const char *TAG = "light";

#if CONFIG_SCCB_HARDWARE_I2C_PORT1
#define SCCB_PORT 1
#else
#define SCCB_PORT 0
#endif

#define LTR308_ADDR      0x53
#define REG_MAIN_CTRL    0x00   // bit 1 = ALS enable
#define REG_MEAS_RATE    0x04   // bits 6:4 resolution, bits 2:0 repeat rate
#define REG_GAIN         0x05
#define REG_PART_ID      0x06   // reads 0xB1
#define REG_DATA_0       0x0D   // 0x0D..0x0F: 20-bit count, low byte first

// 18-bit / 100 ms conversion, new value every 100 ms (the chip's default).
#define MEAS_RATE_VALUE  0x22
#define INT_FACTOR       1.0f   // datasheet: 1 for 100 ms
// Gain 9x (register value 3): 0.067 lux per count, saturates near 17,000 lux.
// Fine for indoors and night; direct sunlight saturates, which still reads as "day".
#define GAIN_VALUE       3
#define GAIN_FACTOR      9.0f

static i2c_master_dev_handle_t s_dev;

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 50);
}

esp_err_t ns_light_init(void)
{
    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_master_get_bus_handle(SCCB_PORT, &bus);   // the camera's bus
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "camera I2C bus not found (call after ns_cam_init)");
        return err;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LTR308_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t reg = REG_PART_ID;
    uint8_t id = 0;
    err = i2c_master_transmit_receive(s_dev, &reg, 1, &id, 1, 50);
    if (err != ESP_OK || id != 0xB1) {
        ESP_LOGW(TAG, "LTR-308 not answering (err %s, part ID 0x%02x)", esp_err_to_name(err), id);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return err != ESP_OK ? err : ESP_ERR_NOT_FOUND;
    }

    err = write_reg(REG_MEAS_RATE, MEAS_RATE_VALUE);
    if (err == ESP_OK) err = write_reg(REG_GAIN, GAIN_VALUE);
    if (err == ESP_OK) err = write_reg(REG_MAIN_CTRL, 0x02);   // start measuring
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LTR-308 setup failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "LTR-308 ready (gain 9x, 100 ms)");
    return ESP_OK;
}

esp_err_t ns_light_read_lux(float *lux)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    // Read all three data bytes in one transfer, so they belong to the same measurement.
    uint8_t reg = REG_DATA_0;
    uint8_t d[3];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, d, sizeof(d), 50);
    if (err != ESP_OK) {
        return err;
    }
    uint32_t counts = d[0] | (d[1] << 8) | ((uint32_t)(d[2] & 0x0F) << 16);
    *lux = 0.6f * counts / (GAIN_FACTOR * INT_FACTOR);
    return ESP_OK;
}
