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
    {0x14, "GT911 Touch (alt)"},
    {0x5D, "GT911 Touch"},
    {0x38, "FT5x06 Touch"},
    {0x18, "ES8311 Audio Codec"},
    {0x19, "RX8025T RTC"},
    {0x48, "ADS1015 ADC / PCF8574 I/O"},
    {0x51, "PCF8563 RTC / AT24C EEPROM"},
    {0x68, "MPU6050 IMU / DS1307/DS3231 RTC"},
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
    ESP_LOGI(TAG, "Scanning I2C bus (0x08 - 0x77)...");
    ESP_LOGI(TAG, "Note: 0x00-0x07 and 0x78-0x7F are reserved");
    ESP_LOGI(TAG, "");

    int devices_found = 0;

    // Scansiona solo indirizzi validi (0x08-0x77)
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        // Crea device temporaneo per testare
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000, // 100kHz per sicurezza
        };

        i2c_master_dev_handle_t dev_handle;
        esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
        
        if (ret == ESP_OK) {
            // Prova a fare una lettura dummy con timeout breve
            uint8_t dummy_data;
            ret = i2c_master_receive(dev_handle, &dummy_data, 1, 50); // 50ms timeout
            
            // Rimuovi device temporaneo SUBITO
            i2c_master_bus_rm_device(dev_handle);
            
            if (ret == ESP_OK || ret == ESP_ERR_TIMEOUT) {
                devices_found++;
                
                // Cerca nome conosciuto
                const char *device_name = "Unknown device";
                for (int i = 0; i < sizeof(known_devices) / sizeof(known_devices[0]); i++) {
                    if (known_devices[i].addr == addr) {
                        device_name = known_devices[i].name;
                        break;
                    }
                }
                
                ESP_LOGI(TAG, "[0x%02X] FOUND: %s", addr, device_name);
            } else if (ret != ESP_ERR_NOT_FOUND) {
                // Errore diverso da "device not found" - possibile problema bus
                ESP_LOGW(TAG, "[0x%02X] Error 0x%x (skipping remaining addresses)", addr, ret);
                break; // Ferma scan se il bus ha problemi
            }
        }
        
        // Delay per dare tempo al bus di recuperare
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Scan complete: %d device(s) found", devices_found);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    
    // Riepilogo devices trovati con gli indirizzi corretti
    ESP_LOGI(TAG, "Expected devices on this board:");
    ESP_LOGI(TAG, "  0x14 = GT911 Touch (INT=HIGH during reset)");
    ESP_LOGI(TAG, "  0x18 = ES8311 Audio Codec");
    ESP_LOGI(TAG, "  0x19 = RX8025T RTC (7-bit address)");
    ESP_LOGI(TAG, "");
}
