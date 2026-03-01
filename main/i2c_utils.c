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
    {0x48, "ADS1015 ADC"},
    {0x68, "MPU6050 IMU / DS1307 RTC"},
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
    ESP_LOGI(TAG, "Scanning I2C bus (0x00 - 0x7F)...");
    ESP_LOGI(TAG, "");

    int devices_found = 0;

    for (uint8_t addr = 0x00; addr <= 0x7F; addr++) {
        // Crea device temporaneo per testare
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000, // 100kHz per sicurezza
        };

        i2c_master_dev_handle_t dev_handle;
        esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
        
        if (ret == ESP_OK) {
            // Prova a fare una lettura dummy
            uint8_t dummy_data;
            ret = i2c_master_receive(dev_handle, &dummy_data, 1, 100);
            
            // Rimuovi device temporaneo
            i2c_master_bus_rm_device(dev_handle);
            
            if (ret == ESP_OK || ret == ESP_ERR_TIMEOUT) {
                // Device risponde (anche se timeout, significa che ha ACKato l'indirizzo)
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
            }
        }
        
        // Piccolo delay per non sovraccaricare il bus
        if (addr % 16 == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Scan complete: %d device(s) found", devices_found);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
}
