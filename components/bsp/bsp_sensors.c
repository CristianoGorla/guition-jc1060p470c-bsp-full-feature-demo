#include "bsp_sensors.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BSP_LOG_TAG "BSP"
#define BSP_PANEL_FMT "| %-10s | "
#define LOG_UNIT "SENSORS"
#define LOGI(fmt, ...) ESP_LOGI(BSP_LOG_TAG, BSP_PANEL_FMT fmt, LOG_UNIT, ##__VA_ARGS__)
#define LOGW(fmt, ...) ESP_LOGW(BSP_LOG_TAG, BSP_PANEL_FMT fmt, LOG_UNIT, ##__VA_ARGS__)
#define LOGE(fmt, ...) ESP_LOGE(BSP_LOG_TAG, BSP_PANEL_FMT fmt, LOG_UNIT, ##__VA_ARGS__)

#define BSP_I2C_SPEED_HZ               100000
#define BSP_I2C_PROBE_TIMEOUT_MS       50
#define AHT20_ADDR                     0x38
#define BMP280_ADDR_PRIMARY            0x77
#define BMP280_ADDR_SECONDARY          0x76

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
#define BMP280_REG_PRESS_MSB           0xF7
#define BMP280_REG_TEMP_MSB            0xFA
#define BMP280_CHIP_ID                 0x58
#define BMP280_CTRL_FORCED_X1          0x25
#define BMP280_MEASUREMENT_DELAY_MS    10

typedef struct {
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
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
static float s_last_valid_humidity_pct = NAN;
static float s_last_valid_pressure_hpa = NAN;

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
    static const uint8_t known_addresses[] = {0x14, 0x18, 0x32, 0x36, 0x38, 0x77, 0x76};

    LOGI("I2C0 scan on SDA=7 SCL=8 (shared bus)");
    for (size_t i = 0; i < sizeof(known_addresses) / sizeof(known_addresses[0]); i++) {
        esp_err_t ret = bsp_i2c_probe(known_addresses[i]);
        if (ret == ESP_OK) {
            LOGI("I2C device detected @ 0x%02X", known_addresses[i]);
        } else {
            LOGW("I2C device not responding @ 0x%02X", known_addresses[i]);
        }
    }
}

static esp_err_t bsp_aht20_init(void)
{
    esp_err_t ret = bsp_i2c_probe(AHT20_ADDR);
    if (ret != ESP_OK) {
        LOGW("AHT20 not found at 0x%02X", AHT20_ADDR);
        return ret;
    }

    ret = bsp_i2c_add_device(AHT20_ADDR, &s_aht20_dev);
    if (ret != ESP_OK) {
        LOGE("Failed to add AHT20 I2C device: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t init_cmd[3] = {AHT20_CMD_INIT_CALIB_0, AHT20_CMD_INIT_CALIB_1, AHT20_CMD_INIT_CALIB_2};
    ret = i2c_master_transmit(s_aht20_dev, init_cmd, sizeof(init_cmd), -1);
    if (ret != ESP_OK) {
        LOGE("AHT20 init command failed: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(s_aht20_dev);
        s_aht20_dev = NULL;
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    s_aht20_ready = true;
    LOGI("AHT20 ready @ 0x%02X", AHT20_ADDR);
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
        LOGW("BMP280 not found at 0x%02X or 0x%02X", BMP280_ADDR_PRIMARY, BMP280_ADDR_SECONDARY);
        return ret;
    }

    ret = bsp_i2c_add_device(bmp_addr, &s_bmp280_dev);
    if (ret != ESP_OK) {
        LOGE("Failed to add BMP280 I2C device: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t chip_id = 0;
    ret = bsp_bmp280_read_reg(BMP280_REG_CHIP_ID, &chip_id, 1);
    if (ret != ESP_OK || chip_id != BMP280_CHIP_ID) {
        LOGE("BMP280 chip id check failed (id=0x%02X, err=%s)", chip_id, esp_err_to_name(ret));
        i2c_master_bus_rm_device(s_bmp280_dev);
        s_bmp280_dev = NULL;
        return ESP_FAIL;
    }

    uint8_t calib_buf[24] = {0};
    ret = bsp_bmp280_read_reg(BMP280_REG_CALIB_START, calib_buf, sizeof(calib_buf));
    if (ret != ESP_OK) {
        LOGE("BMP280 calibration read failed: %s", esp_err_to_name(ret));
        i2c_master_bus_rm_device(s_bmp280_dev);
        s_bmp280_dev = NULL;
        return ret;
    }

    s_bmp280_calib.dig_T1 = u16_le(&calib_buf[0]);
    s_bmp280_calib.dig_T2 = s16_le(&calib_buf[2]);
    s_bmp280_calib.dig_T3 = s16_le(&calib_buf[4]);
    s_bmp280_calib.dig_P1 = u16_le(&calib_buf[6]);
    s_bmp280_calib.dig_P2 = s16_le(&calib_buf[8]);
    s_bmp280_calib.dig_P3 = s16_le(&calib_buf[10]);
    s_bmp280_calib.dig_P4 = s16_le(&calib_buf[12]);
    s_bmp280_calib.dig_P5 = s16_le(&calib_buf[14]);
    s_bmp280_calib.dig_P6 = s16_le(&calib_buf[16]);
    s_bmp280_calib.dig_P7 = s16_le(&calib_buf[18]);
    s_bmp280_calib.dig_P8 = s16_le(&calib_buf[20]);
    s_bmp280_calib.dig_P9 = s16_le(&calib_buf[22]);

    s_bmp280_ready = true;
    LOGI("BMP280 ready @ 0x%02X", bmp_addr);
    return ESP_OK;
}

static esp_err_t bsp_tsens_init(void)
{
    temperature_sensor_config_t tsens_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t ret = temperature_sensor_install(&tsens_cfg, &s_tsens_handle);
    if (ret != ESP_OK) {
        LOGW("TSENS install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = temperature_sensor_enable(s_tsens_handle);
    if (ret != ESP_OK) {
        LOGW("TSENS enable failed: %s", esp_err_to_name(ret));
        temperature_sensor_uninstall(s_tsens_handle);
        s_tsens_handle = NULL;
        return ret;
    }

    s_tsens_ready = true;
    LOGI("TSENS fallback ready");
    return ESP_OK;
}

static esp_err_t bsp_aht20_read_temp_humidity(float *temp_c, float *humidity_pct)
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

    uint32_t humidity_raw = ((uint32_t)data[1] << 12) |
                            ((uint32_t)data[2] << 4) |
                            (((uint32_t)data[3] & 0xF0) >> 4);

    uint32_t temp_raw = ((uint32_t)(data[3] & 0x0F) << 16) |
                        ((uint32_t)data[4] << 8) |
                        data[5];

    *humidity_pct = ((float)humidity_raw * 100.0f) / 1048576.0f;
    *temp_c = ((float)temp_raw * 200.0f / 1048576.0f) - 50.0f;
    return ESP_OK;
}

static esp_err_t bsp_bmp280_read_temp_pressure(float *temp_c, float *pressure_hpa)
{
    uint8_t ctrl_meas[2] = {BMP280_REG_CTRL_MEAS, BMP280_CTRL_FORCED_X1};
    esp_err_t ret = i2c_master_transmit(s_bmp280_dev, ctrl_meas, sizeof(ctrl_meas), -1);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(BMP280_MEASUREMENT_DELAY_MS));

    uint8_t press_temp_regs[6] = {0};
    ret = bsp_bmp280_read_reg(BMP280_REG_PRESS_MSB, press_temp_regs, sizeof(press_temp_regs));
    if (ret != ESP_OK) {
        return ret;
    }

    int32_t adc_p = ((int32_t)press_temp_regs[0] << 12) |
                    ((int32_t)press_temp_regs[1] << 4) |
                    ((int32_t)press_temp_regs[2] >> 4);
    int32_t adc_t = ((int32_t)press_temp_regs[3] << 12) |
                    ((int32_t)press_temp_regs[4] << 4) |
                    ((int32_t)press_temp_regs[5] >> 4);

    if (adc_t == 0x80000 || adc_p == 0x80000) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    double var1 = (((double)adc_t) / 16384.0 - ((double)s_bmp280_calib.dig_T1) / 1024.0) *
                  ((double)s_bmp280_calib.dig_T2);
    double var2 = ((((double)adc_t) / 131072.0 - ((double)s_bmp280_calib.dig_T1) / 8192.0) *
                   (((double)adc_t) / 131072.0 - ((double)s_bmp280_calib.dig_T1) / 8192.0)) *
                  ((double)s_bmp280_calib.dig_T3);
    double t_fine = var1 + var2;
    *temp_c = (float)(t_fine / 5120.0);

    var1 = (t_fine / 2.0) - 64000.0;
    var2 = var1 * var1 * ((double)s_bmp280_calib.dig_P6) / 32768.0;
    var2 = var2 + var1 * ((double)s_bmp280_calib.dig_P5) * 2.0;
    var2 = (var2 / 4.0) + (((double)s_bmp280_calib.dig_P4) * 65536.0);
    var1 = ((((double)s_bmp280_calib.dig_P3) * var1 * var1 / 524288.0) +
            (((double)s_bmp280_calib.dig_P2) * var1)) / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * ((double)s_bmp280_calib.dig_P1);

    if (var1 == 0.0) {
        return ESP_ERR_INVALID_STATE;
    }

    double pressure_pa = 1048576.0 - (double)adc_p;
    pressure_pa = (pressure_pa - (var2 / 4096.0)) * 6250.0 / var1;
    var1 = ((double)s_bmp280_calib.dig_P9) * pressure_pa * pressure_pa / 2147483648.0;
    var2 = pressure_pa * ((double)s_bmp280_calib.dig_P8) / 32768.0;
    pressure_pa = pressure_pa + (var1 + var2 + ((double)s_bmp280_calib.dig_P7)) / 16.0;

    *pressure_hpa = (float)(pressure_pa / 100.0);
    if (*pressure_hpa < 300.0f || *pressure_hpa > 1200.0f) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t bsp_tsens_read_temp(float *temp_c)
{
    if (!s_tsens_ready || s_tsens_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return temperature_sensor_get_celsius(s_tsens_handle, temp_c);
}

esp_err_t bsp_sensors_init(i2c_master_bus_handle_t i2c_bus_handle)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_bus_handle = i2c_bus_handle;
    if (s_bus_handle == NULL) {
        LOGE("I2C bus is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    bsp_scan_known_i2c_devices();

    if (bsp_aht20_init() != ESP_OK) {
        LOGW("AHT20 unavailable, will fallback to BMP280/TSENS");
    }

    if (bsp_bmp280_init() != ESP_OK) {
        LOGW("BMP280 unavailable, will fallback to TSENS");
    }

    if (bsp_tsens_init() != ESP_OK) {
        LOGW("TSENS fallback not available");
    }

    if (s_aht20_ready && s_bmp280_ready) {
        LOGI("[OK] AHT20+BMP280 initialized");
    } else {
        LOGW("[WARN] Sensors partial init (AHT20=%d BMP280=%d)", (int)s_aht20_ready, (int)s_bmp280_ready);
    }

    s_initialized = true;
    return (s_aht20_ready || s_bmp280_ready || s_tsens_ready) ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_sensor_get_data(bsp_sensor_data_t *out_data)
{
    float temp_c = NAN;
    float humidity_pct = NAN;
    float pressure_hpa = NAN;

    if (out_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_data, 0, sizeof(*out_data));

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_aht20_ready && bsp_aht20_read_temp_humidity(&temp_c, &humidity_pct) == ESP_OK) {
        out_data->temperature_c = temp_c;
        out_data->humidity_pct = humidity_pct;
        out_data->has_temperature = true;
        out_data->has_humidity = true;
        s_last_valid_temp_c = temp_c;
        s_last_valid_humidity_pct = humidity_pct;
    }

    if (s_bmp280_ready && bsp_bmp280_read_temp_pressure(&temp_c, &pressure_hpa) == ESP_OK) {
        if (!out_data->has_temperature) {
            out_data->temperature_c = temp_c;
            out_data->has_temperature = true;
            s_last_valid_temp_c = temp_c;
        }
        out_data->pressure_hpa = pressure_hpa;
        out_data->has_pressure = true;
        s_last_valid_pressure_hpa = pressure_hpa;
    }

    if (!out_data->has_temperature && bsp_tsens_read_temp(&temp_c) == ESP_OK) {
        out_data->temperature_c = temp_c;
        out_data->has_temperature = true;
        s_last_valid_temp_c = temp_c;
    }

    if (!out_data->has_temperature && !isnan(s_last_valid_temp_c)) {
        out_data->temperature_c = s_last_valid_temp_c;
        out_data->has_temperature = true;
    }
    if (!out_data->has_humidity && !isnan(s_last_valid_humidity_pct)) {
        out_data->humidity_pct = s_last_valid_humidity_pct;
        out_data->has_humidity = true;
    }
    if (!out_data->has_pressure && !isnan(s_last_valid_pressure_hpa)) {
        out_data->pressure_hpa = s_last_valid_pressure_hpa;
        out_data->has_pressure = true;
    }

    return out_data->has_temperature ? ESP_OK : ESP_FAIL;
}

float bsp_sensor_get_temp(void)
{
    bsp_sensor_data_t data = {0};

    if (bsp_sensor_get_data(&data) == ESP_OK && data.has_temperature) {
        return data.temperature_c;
    }

    return s_last_valid_temp_c;
}
