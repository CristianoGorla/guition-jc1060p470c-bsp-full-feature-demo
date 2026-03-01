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

// Nomi degli errori per debug
static const char* get_error_name(esp_err_t err) {
    switch(err) {
        case ESP_OK: return "ESP_OK";
        case ESP_ERR_TIMEOUT: return "TIMEOUT";
        case ESP_ERR_INVALID_STATE: return "INVALID_STATE";
        case ESP_ERR_NOT_FOUND: return "NOT_FOUND";
        case ESP_FAIL: return "FAIL";
        default: return "UNKNOWN";
    }
}

/**
 * Scan mirato solo agli indirizzi noti
 * Evita il problema del bus I2C ESP32-P4 che va in INVALID_STATE
 */
void i2c_scan_bus(i2c_master_bus_handle_t bus_handle)
{
    if (!bus_handle) {
        ESP_LOGE(TAG, "Bus handle is NULL!");
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   I2C BUS SCANNER (Targeted Mode)");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Scanning only known device addresses...");
    ESP_LOGI(TAG, "Note: Full scan causes ESP32-P4 I2C bus issues");
    ESP_LOGI(TAG, "");

    int devices_found = 0;
    int errors_found = 0;

    // Scansiona SOLO gli indirizzi dei dispositivi conosciuti
    for (int i = 0; i < sizeof(known_devices) / sizeof(known_devices[0]); i++) {
        uint8_t addr = known_devices[i].addr;
        const char *device_name = known_devices[i].name;
        
        ESP_LOGI(TAG, "Probing 0x%02X (%s)...", addr, device_name);
        
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
            ret = i2c_master_receive(dev_handle, &dummy_data, 1, 100); // 100ms timeout
            
            // Rimuovi device temporaneo SUBITO
            i2c_master_bus_rm_device(dev_handle);
            
            if (ret == ESP_OK) {
                devices_found++;
                ESP_LOGI(TAG, "  → [0x%02X] ✓ FOUND: %s", addr, device_name);
            } else if (ret == ESP_ERR_TIMEOUT) {
                devices_found++;
                ESP_LOGI(TAG, "  → [0x%02X] ✓ FOUND: %s (ACK but timeout)", addr, device_name);
            } else if (ret == ESP_ERR_NOT_FOUND) {
                ESP_LOGW(TAG, "  → [0x%02X] ✗ Not responding", addr);
            } else {
                errors_found++;
                ESP_LOGE(TAG, "  → [0x%02X] ✗ ERROR: %s (0x%x)", addr, get_error_name(ret), ret);
            }
        } else {
            errors_found++;
            ESP_LOGE(TAG, "  → [0x%02X] ✗ ADD_DEVICE ERROR: %s (0x%x)", addr, get_error_name(ret), ret);
        }
        
        // Delay più lungo per dare tempo al bus di recuperare
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Scan complete:");
    ESP_LOGI(TAG, "  Devices found: %d / %d", devices_found, sizeof(known_devices) / sizeof(known_devices[0]));
    if (errors_found > 0) {
        ESP_LOGW(TAG, "  Errors encountered: %d", errors_found);
    }
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    
    // Riepilogo
    ESP_LOGI(TAG, "Expected devices on this board:");
    ESP_LOGI(TAG, "  0x14 = GT911 Touch (INT=HIGH during reset)");
    ESP_LOGI(TAG, "  0x18 = ES8311 Audio Codec");
    ESP_LOGI(TAG, "  0x19 = RX8025T RTC");
    ESP_LOGI(TAG, "");
    
    if (devices_found >= 2) {
        ESP_LOGI(TAG, "✓ Bus is operational, found %d device(s)", devices_found);
    } else {
        ESP_LOGW(TAG, "⚠ Expected at least 2 devices, found %d", devices_found);
    }
    ESP_LOGI(TAG, "");
}
