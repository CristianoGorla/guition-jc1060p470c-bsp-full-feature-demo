#include "bsp_sensors.h"

#include <math.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "BSP_SENSORS"

#define BSP_I2C_SPEED_HZ               100000
#define BSP_I2C_PROBE_TIMEOUT_MS       50
#define AHT20_ADDR                     0x38
#define BMP280_ADDR_PRIMARY            0x76
#define BMP280_ADDR_SECONDARY          0x77

#define AHT20_CMD_INIT_CALIB_0         0xBE
#define AHT20_CMD_INIT_CALIB_1         0x08
#define AHT20_CMD_INIT_CALIB_2         0x00
#define AHT20_CMD_TRIGGER_0            0xAC
#define AHT20_CMD_TRIGGER_1            0x33
#define AHT20_CMD_TRIGGER_2            0x00
#define AHT20_STATUS_BUSY_MASK         0x80
#define AHT20_MEASUREMENT_DELAY_MS     80

#define BMP280_REG_CALIB_START         0x88
#define BMP280_REG_CHIP_ID             0xD0
#define BMP280_REG_CTRL_MEAS           0xF4
#define BMP280_REG_TEMP_MSB            0xFA
#define BMP280_CHIP_ID                 0x58
#define BMP280_CTRL_FORCED_X1          0x25
#define BMP280_MEASUREMENT_DELAY_MS    10

typedef struct {
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
} bmp280_calib_t;

static i2c_master_bus_handle_t s_bus_handle;
static i2c_master_dev_handle_t s_aht20_dev;
static i2c_master_dev_handle_t s_bmp280_dev;
static temperature_sensor_handle_t s_tsens_handle;

static bmp280_calib_t s_bmp280_calib;
static bool s_initialized;
static bool s_aht20_ready;
static bool s_bmp280_ready;
static bool s_tsens_ready;
static float s_last_valid_temp_c = NAN;

/* Provided by guition_jc1060_bsp component. */
extern i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void);

static inline uint16_t u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static inline int16_t s16_le(const uint8_t *data)
{
    return (int16_t)u16_le(data);
}

static esp_err_t bsp_i2c_add_device(uint8_t addr, i2c_master_dev_handle_t *out_dev)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BSP_I2C_SPEED_HZ,
    };
    return i2c_master_bus_add_device(s_bus_handle, &dev_cfg, out_dev);
}

static esp_err_t bsp_i2c_probe(uint8_t addr)
{
    return i2c_master_probe(s_bus_handle, addr, BSP_I2C_PROBE_TIMEOUT_MS);
}

static void bsp_scan_known_i2c_devices(void)
{
    static const uint8_t known_addresses[] = {0x14, 0x18, 0x32, 0x36, 0x38, 0x76, 0x77};

    ESP_LOGI(TAG, "I2C0 scan on SDA=7 SCL=8 (shared bus)");
    for (size_t i = 0; i < sizeof(known_addresses) / sizeof(known_addresses[0]); i++) {
        esp_err_t ret = bsp_i2c_probe(known_addresses[i]);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "I2C device detected @ 0x%02X", known_addresses[i]);
        } else {
            ESP_LOGW(TAG, "I2C device not responding @ 0x%02X", known_addresses[i]);
        }
    }
}

static esp_err_t bsp_aht20_init(void)
{
    esp_err_t ret = bsp_i2c_probe(AHT20_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AHT20 not found at 0x%02X", AHT20_ADDR);
        return ret;
    }

    ret = bsp_i2c_add_device(AHT20_ADDR, &s_aht20_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add AHT20 I2C device: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t init_cmd[3] = {AHT20_CMD_INIT_CALIB_0, AHT20_CMD_INIT_CALIB_1, AHT20_CMD_INIT_CALIB_2};
    ret = i2c_master_transmit(s_aht20_dev, init_cmd, sizeof(init_cmd), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AHT20 init command failed: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(s_aht20_dev);
        s_aht20_dev = NULL;
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    s_aht20_ready = true;
    ESP_LOGI(TAG, "AHT20 ready @ 0x%02X", AHT20_ADDR);
    return ESP_OK;
}

static esp_err_t bsp_bmp280_read_reg(uint8_t reg, uint8_t *data, size_t data_len)
{
    return i2c_master_transmit_receive(s_bmp280_dev, &reg, 1, data, data_len, -1);
}

static esp_err_t bsp_bmp280_init(void)
{
    uint8_t bmp_addr = BMP280_ADDR_PRIMARY;
    esp_err_t ret = bsp_i2c_probe(BMP280_ADDR_PRIMARY);

    if (ret != ESP_OK) {
        ret = bsp_i2c_probe(BMP280_ADDR_SECONDARY);
        bmp_addr = BMP280_ADDR_SECONDARY;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BMP280 not found at 0x%02X or 0x%02X", BMP280_ADDR_PRIMARY, BMP280_ADDR_SECONDARY);
        return ret;
    }

    ret = bsp_i2c_add_device(bmp_addr, &s_bmp280_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add BMP280 I2C device: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t chip_id = 0;
    ret = bsp_bmp280_read_reg(BMP280_REG_CHIP_ID, &chip_id, 1);
    if (ret != ESP_OK || chip_id != BMP280_CHIP_ID) {
        ESP_LOGE(TAG, "BMP280 chip id check failed (id=0x%02X, err=%s)", chip_id, esp_err_to_name(ret));
        i2c_master_bus_rm_device(s_bmp280_dev);
        s_bmp280_dev = NULL;
        return ESP_FAIL;
    }

    uint8_t calib_buf[6] = {0};
    ret = bsp_bmp280_read_reg(BMP280_REG_CALIB_START, calib_buf, sizeof(calib_buf));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BMP280 calibration read failed: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(s_bmp280_dev);
        s_bmp280_dev = NULL;
        return ret;
    }

    s_bmp280_calib.dig_T1 = u16_le(&calib_buf[0]);
    s_bmp280_calib.dig_T2 = s16_le(&calib_buf[2]);
    s_bmp280_calib.dig_T3 = s16_le(&calib_buf[4]);

    s_bmp280_ready = true;
    ESP_LOGI(TAG, "BMP280 ready @ 0x%02X", bmp_addr);
    return ESP_OK;
}

static esp_err_t bsp_tsens_init(void)
{
    temperature_sensor_config_t tsens_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t ret = temperature_sensor_install(&tsens_cfg, &s_tsens_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "TSENS install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = temperature_sensor_enable(s_tsens_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "TSENS enable failed: %s", esp_err_to_name(ret));
        temperature_sensor_uninstall(s_tsens_handle);
        s_tsens_handle = NULL;
        return ret;
    }

    s_tsens_ready = true;
    ESP_LOGI(TAG, "TSENS fallback ready");
    return ESP_OK;
}

static esp_err_t bsp_aht20_read_temp(float *temp_c)
{
    uint8_t cmd[3] = {AHT20_CMD_TRIGGER_0, AHT20_CMD_TRIGGER_1, AHT20_CMD_TRIGGER_2};
    uint8_t data[7] = {0};

    esp_err_t ret = i2c_master_transmit(s_aht20_dev, cmd, sizeof(cmd), -1);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(AHT20_MEASUREMENT_DELAY_MS));

    ret = i2c_master_receive(s_aht20_dev, data, sizeof(data), -1);
    if (ret != ESP_OK) {
        return ret;
    }

    if ((data[0] & AHT20_STATUS_BUSY_MASK) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t temp_raw = ((uint32_t)(data[3] & 0x0F) << 16) |
                        ((uint32_t)data[4] << 8) |
                        data[5];

    *temp_c = ((float)temp_raw * 200.0f / 1048576.0f) - 50.0f;
    return ESP_OK;
}

static esp_err_t bsp_bmp280_read_temp(float *temp_c)
{
    uint8_t ctrl_meas[2] = {BMP280_REG_CTRL_MEAS, BMP280_CTRL_FORCED_X1};
    esp_err_t ret = i2c_master_transmit(s_bmp280_dev, ctrl_meas, sizeof(ctrl_meas), -1);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(BMP280_MEASUREMENT_DELAY_MS));

    uint8_t temp_regs[3] = {0};
    ret = bsp_bmp280_read_reg(BMP280_REG_TEMP_MSB, temp_regs, sizeof(temp_regs));
    if (ret != ESP_OK) {
        return ret;
    }

    int32_t adc_t = ((int32_t)temp_regs[0] << 12) |
                    ((int32_t)temp_regs[1] << 4) |
                    ((int32_t)temp_regs[2] >> 4);

    int32_t var1 = ((((adc_t >> 3) - ((int32_t)s_bmp280_calib.dig_T1 << 1))) * ((int32_t)s_bmp280_calib.dig_T2)) >> 11;
    int32_t var2 = (((((adc_t >> 4) - ((int32_t)s_bmp280_calib.dig_T1)) *
                      ((adc_t >> 4) - ((int32_t)s_bmp280_calib.dig_T1))) >> 12) *
                    ((int32_t)s_bmp280_calib.dig_T3)) >> 14;
    int32_t t_fine = var1 + var2;
    int32_t t = (t_fine * 5 + 128) >> 8;

    *temp_c = (float)t / 100.0f;
    return ESP_OK;
}

static esp_err_t bsp_tsens_read_temp(float *temp_c)
{
    if (!s_tsens_ready || s_tsens_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return temperature_sensor_get_celsius(s_tsens_handle, temp_c);
}

esp_err_t bsp_sensors_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_bus_handle = bsp_i2c_get_bus_handle();
    if (s_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    bsp_scan_known_i2c_devices();

    if (bsp_aht20_init() != ESP_OK) {
        ESP_LOGW(TAG, "AHT20 unavailable, will fallback to BMP280/TSENS");
    }

    if (bsp_bmp280_init() != ESP_OK) {
        ESP_LOGW(TAG, "BMP280 unavailable, will fallback to TSENS");
    }

    if (bsp_tsens_init() != ESP_OK) {
        ESP_LOGW(TAG, "TSENS fallback not available");
    }

    s_initialized = true;
    return (s_aht20_ready || s_bmp280_ready || s_tsens_ready) ? ESP_OK : ESP_FAIL;
}

float bsp_sensor_get_temp(void)
{
    float temp_c = NAN;

    if (!s_initialized) {
        (void)bsp_sensors_init();
    }

    if (s_aht20_ready && bsp_aht20_read_temp(&temp_c) == ESP_OK) {
        s_last_valid_temp_c = temp_c;
        return temp_c;
    }

    if (s_bmp280_ready && bsp_bmp280_read_temp(&temp_c) == ESP_OK) {
        s_last_valid_temp_c = temp_c;
        return temp_c;
    }

    if (bsp_tsens_read_temp(&temp_c) == ESP_OK) {
        s_last_valid_temp_c = temp_c;
        return temp_c;
    }

    return s_last_valid_temp_c;
}
