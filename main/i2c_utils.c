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

    printf("\n");
    printf("\033[36m========================================\033[0m\n");
    printf("\033[36m   I2C BUS SCANNER (Probe Mode)\033[0m\n");
    printf("\033[36m========================================\033[0m\n");
    ESP_LOGI(TAG, "Scanning 0x08 to 0x77...");
    ESP_LOGI(TAG, "Using lightweight probe (add_device only)");
    printf("\n");

    int devices_found = 0;
    int probe_attempts = 0;

    // Scan completo con approccio ultra-leggero
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        probe_attempts++;
        
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,
        };

        i2c_master_dev_handle_t dev_handle;
        
        // Prova SOLO ad aggiungere il device (no receive!)
        // Se add_device funziona, il device potrebbe essere presente
        esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
        
        if (ret == ESP_OK) {
            // Device handle creato - prova una singola transazione velocissima
            uint8_t dummy;
            esp_err_t read_ret = i2c_master_receive(dev_handle, &dummy, 1, 10); // 10ms timeout cortissimo
            
            // Rimuovi IMMEDIATAMENTE
            i2c_master_bus_rm_device(dev_handle);
            
            // Considera trovato solo se riceve ACK o TIMEOUT (= ACK + no data)
            if (read_ret == ESP_OK || read_ret == ESP_ERR_TIMEOUT) {
                devices_found++;
                
                const char *device_name = NULL;
                for (int i = 0; i < sizeof(known_devices) / sizeof(known_devices[0]); i++) {
                    if (known_devices[i].addr == addr) {
                        device_name = known_devices[i].name;
                        break;
                    }
                }
                
                if (device_name) {
                    printf("\033[32m[0x%02X] ✓ %s\033[0m\n", addr, device_name);
                } else {
                    printf("\033[32m[0x%02X] ✓ Unknown\033[0m\n", addr);
                }
            }
            // Altri errori = silenzioso
        }
        
        // Delay ridotto ma non zero
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    printf("\n");
    printf("\033[36m========================================\033[0m\n");
    printf("\033[32mDevices found: %d\033[0m\n", devices_found);
    printf("Addresses probed: %d\n", probe_attempts);
    printf("\033[36m========================================\033[0m\n");
    printf("\n");
    
    ESP_LOGI(TAG, "Board devices:");
    ESP_LOGI(TAG, "  0x14 = GT911 Touch");
    ESP_LOGI(TAG, "  0x18 = ES8311 Audio");
    ESP_LOGI(TAG, "  0x32 = RX8025T RTC");
    
    if (devices_found < 2) {
        ESP_LOGW(TAG, "Warning: Expected at least 2 devices (GT911 + ES8311)");
    }
    printf("\n");
}
