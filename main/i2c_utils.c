#include "i2c_utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "I2C_SCAN";

// Known I2C device addresses to scan
// ESP32-P4 ha problemi con full scan, scansiona solo range sicuri
static const uint8_t scan_addresses[] = {
    // Touch controllers
    0x14, 0x5D, 0x38,
    // Audio
    0x18,
    // RTC range
    0x32, 0x51, 0x68,
    // Sensors
    0x48, 0x6F, 0x76, 0x77,
    // Display
    0x3C,
};

static const struct {
    uint8_t addr;
    const char *name;
} known_devices[] = {
    {0x14, "GT911 Touch (INT=HIGH)"},
    {0x5D, "GT911 Touch (INT=LOW)"},
    {0x38, "FT5x06 Touch"},
    {0x18, "ES8311 Audio Codec"},
    {0x32, "RX8025T RTC"},
    {0x48, "ADS1015 ADC / PCF8574"},
    {0x51, "PCF8563 RTC / EEPROM"},
    {0x68, "MPU6050 / DS1307 RTC"},
    {0x6F, "BQ27220 Fuel Gauge"},
    {0x76, "BME280 Sensor"},
    {0x77, "BMP280 Sensor"},
    {0x3C, "SSD1306 OLED"},
};

void i2c_scan_bus(i2c_master_bus_handle_t bus_handle)
{
    if (!bus_handle) {
        ESP_LOGE(TAG, "Bus handle is NULL!");
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   I2C BUS SCANNER");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Scanning known I2C addresses...");
    ESP_LOGI(TAG, "Note: ESP32-P4 I2C has issues with full scan");
    ESP_LOGI(TAG, "");

    int devices_found = 0;
    int num_addresses = sizeof(scan_addresses) / sizeof(scan_addresses[0]);

    for (int idx = 0; idx < num_addresses; idx++) {
        uint8_t addr = scan_addresses[idx];
        
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,
        };

        i2c_master_dev_handle_t dev_handle;
        esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
        
        if (ret == ESP_OK) {
            uint8_t dummy_data;
            ret = i2c_master_receive(dev_handle, &dummy_data, 1, 100);
            
            i2c_master_bus_rm_device(dev_handle);
            
            if (ret == ESP_OK || ret == ESP_ERR_TIMEOUT) {
                devices_found++;
                
                // Cerca nome
                const char *device_name = "Unknown device";
                for (int i = 0; i < sizeof(known_devices) / sizeof(known_devices[0]); i++) {
                    if (known_devices[i].addr == addr) {
                        device_name = known_devices[i].name;
                        break;
                    }
                }
                
                printf("\033[32m[0x%02X] ✓ %s\033[0m\n", addr, device_name);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // Delay più lungo per sicurezza
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    printf("\033[32mDevices found: %d / %d addresses scanned\033[0m\n", devices_found, num_addresses);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "Board devices:");
    ESP_LOGI(TAG, "  0x14 = GT911 Touch (this board config)");
    ESP_LOGI(TAG, "  0x18 = ES8311 Audio Codec");
    ESP_LOGI(TAG, "  0x32 = RX8025T RTC");
    ESP_LOGI(TAG, "");
}
