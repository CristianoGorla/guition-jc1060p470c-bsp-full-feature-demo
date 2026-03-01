#include "i2c_utils.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "I2C_SCAN";

// Known I2C device addresses
static const struct {
    uint8_t addr;
    const char *name;
} known_devices[] = {
    {0x14, "GT911 Touch (INT=HIGH)"},
    {0x5D, "GT911 Touch (INT=LOW)"},
    {0x38, "FT5x06 Touch"},
    {0x18, "ES8311 Audio Codec"},
    {0x32, "RX8025T RTC"},
    {0x48, "ADS1015 ADC / PCF8574 I/O"},
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
    ESP_LOGI(TAG, "   I2C BUS SCANNER (Full Scan)");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Scanning I2C bus 0x08-0x77...");
    ESP_LOGI(TAG, "");

    int devices_found = 0;
    int errors_found = 0;

    // Scansiona tutti gli indirizzi validi (0x08-0x77)
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,
        };

        i2c_master_dev_handle_t dev_handle;
        esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
        
        if (ret == ESP_OK) {
            uint8_t dummy_data;
            ret = i2c_master_receive(dev_handle, &dummy_data, 1, 50);
            
            i2c_master_bus_rm_device(dev_handle);
            
            if (ret == ESP_OK || ret == ESP_ERR_TIMEOUT) {
                // Device found!
                devices_found++;
                
                // Cerca nome conosciuto
                const char *device_name = NULL;
                for (int i = 0; i < sizeof(known_devices) / sizeof(known_devices[0]); i++) {
                    if (known_devices[i].addr == addr) {
                        device_name = known_devices[i].name;
                        break;
                    }
                }
                
                if (device_name) {
                    // Verde per dispositivi noti
                    printf("\033[32m[0x%02X] ✓ %s\033[0m\n", addr, device_name);
                } else {
                    // Verde per dispositivi sconosciuti
                    printf("\033[32m[0x%02X] ✓ Unknown device\033[0m\n", addr);
                }
                
            } else if (ret == ESP_ERR_INVALID_STATE) {
                // Errore INVALID_STATE (silenzioso - troppo rumore)
                errors_found++;
                
            } else if (ret != ESP_ERR_NOT_FOUND) {
                // Altri errori (rari)
                errors_found++;
                printf("\033[33m[0x%02X] ⚠ Error: 0x%x\033[0m\n", addr, ret);
            }
            // ESP_ERR_NOT_FOUND = nessun device (silenzioso)
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    printf("\033[32mDevices found: %d\033[0m\n", devices_found);
    if (errors_found > 0) {
        printf("\033[33mErrors: %d (INVALID_STATE - bus recovery issues)\033[0m\n", errors_found);
    }
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
}
