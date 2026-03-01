#include "rtc_rx8025t.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RX8025T";
static i2c_master_dev_handle_t rtc_dev_handle = NULL;

// Helper: Convert decimal to BCD
static uint8_t dec_to_bcd(uint8_t val) {
    return ((val / 10) << 4) | (val % 10);
}

// Helper: Convert BCD to decimal
static uint8_t bcd_to_dec(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

esp_err_t rtc_rx8025t_init(i2c_master_bus_handle_t bus_handle)
{
    ESP_LOGI(TAG, "Initializing RX8025T RTC...");
    ESP_LOGI(TAG, "I2C Address: 0x%02X", RX8025T_I2C_ADDR);

    // Create I2C device handle
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RX8025T_I2C_ADDR,
        .scl_speed_hz = 400000,  // 400kHz
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &rtc_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add RTC device to I2C bus (0x%x)", ret);
        return ret;
    }

    // Step 1: Clear PON and VLF flags (Extension Register 0x0F)
    ESP_LOGI(TAG, "Clearing PON/VLF flags...");
    uint8_t clear_flags[] = {RX8025T_REG_CTRL2, 0x00};
    ret = i2c_master_transmit(rtc_dev_handle, clear_flags, sizeof(clear_flags), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear flags (0x%x)", ret);
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    // Step 2: Set 24-hour format (Control Register 1 0x0E)
    ESP_LOGI(TAG, "Setting 24-hour format...");
    uint8_t ctrl1_cfg[] = {RX8025T_REG_CTRL1, RX8025T_CTRL1_24H};
    ret = i2c_master_transmit(rtc_dev_handle, ctrl1_cfg, sizeof(ctrl1_cfg), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set 24h format (0x%x)", ret);
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    // Step 3: Check if time needs to be set (read current time)
    uint8_t reg_addr = RX8025T_REG_SEC;
    uint8_t time_data[7];
    
    ret = i2c_master_transmit_receive(rtc_dev_handle, &reg_addr, 1, time_data, 7, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read time registers (0x%x)", ret);
        return ret;
    }

    // Check if time is valid (non-zero)
    bool time_valid = false;
    for (int i = 0; i < 7; i++) {
        if (time_data[i] != 0) {
            time_valid = true;
            break;
        }
    }

    if (!time_valid) {
        ESP_LOGW(TAG, "RTC time not set, initializing to 2026-03-01 12:00:00");
        rtc_time_t init_time = {
            .second = 0,
            .minute = 0,
            .hour = 12,
            .wday = 6,   // Saturday
            .day = 1,
            .month = 3,
            .year = 26   // 2026
        };
        ret = rtc_rx8025t_set_time(&init_time);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set initial time (0x%x)", ret);
            return ret;
        }
    } else {
        ESP_LOGI(TAG, "RTC time already set");
        // Log current time
        rtc_time_t current;
        if (rtc_rx8025t_get_time(&current) == ESP_OK) {
            ESP_LOGI(TAG, "Current time: 20%02d-%02d-%02d %02d:%02d:%02d",
                     current.year, current.month, current.day,
                     current.hour, current.minute, current.second);
        }
    }

    ESP_LOGI(TAG, "RX8025T initialized successfully");
    return ESP_OK;
}

esp_err_t rtc_rx8025t_set_time(const rtc_time_t *time)
{
    if (!rtc_dev_handle) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Prepare time data in BCD format
    uint8_t time_data[8] = {
        RX8025T_REG_SEC,            // Starting register
        dec_to_bcd(time->second),   // Seconds
        dec_to_bcd(time->minute),   // Minutes
        dec_to_bcd(time->hour),     // Hours
        time->wday,                 // Week day (0-6)
        dec_to_bcd(time->day),      // Day
        dec_to_bcd(time->month),    // Month
        dec_to_bcd(time->year)      // Year (00-99)
    };

    esp_err_t ret = i2c_master_transmit(rtc_dev_handle, time_data, sizeof(time_data), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set time (0x%x)", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Time set: 20%02d-%02d-%02d %02d:%02d:%02d",
             time->year, time->month, time->day,
             time->hour, time->minute, time->second);

    return ESP_OK;
}

esp_err_t rtc_rx8025t_get_time(rtc_time_t *time)
{
    if (!rtc_dev_handle) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg_addr = RX8025T_REG_SEC;
    uint8_t time_data[7];

    esp_err_t ret = i2c_master_transmit_receive(rtc_dev_handle, &reg_addr, 1, time_data, 7, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read time (0x%x)", ret);
        return ret;
    }

    // Convert BCD to decimal
    time->second = bcd_to_dec(time_data[0] & 0x7F);  // Mask bit 7 (not used)
    time->minute = bcd_to_dec(time_data[1] & 0x7F);
    time->hour   = bcd_to_dec(time_data[2] & 0x3F);  // Mask bits 6-7
    time->wday   = time_data[3] & 0x07;              // Only bits 0-2
    time->day    = bcd_to_dec(time_data[4] & 0x3F);
    time->month  = bcd_to_dec(time_data[5] & 0x1F);
    time->year   = bcd_to_dec(time_data[6]);

    return ESP_OK;
}

esp_err_t rtc_rx8025t_check_power_on_flag(bool *pon_flag)
{
    if (!rtc_dev_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg_addr = RX8025T_REG_CTRL2;
    uint8_t ctrl2;

    esp_err_t ret = i2c_master_transmit_receive(rtc_dev_handle, &reg_addr, 1, &ctrl2, 1, 100);
    if (ret != ESP_OK) {
        return ret;
    }

    *pon_flag = (ctrl2 & RX8025T_CTRL2_PON) != 0;
    return ESP_OK;
}

esp_err_t rtc_rx8025t_check_voltage_low_flag(bool *vlf_flag)
{
    if (!rtc_dev_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg_addr = RX8025T_REG_CTRL2;
    uint8_t ctrl2;

    esp_err_t ret = i2c_master_transmit_receive(rtc_dev_handle, &reg_addr, 1, &ctrl2, 1, 100);
    if (ret != ESP_OK) {
        return ret;
    }

    *vlf_flag = (ctrl2 & RX8025T_CTRL2_VLF) != 0;
    return ESP_OK;
}
