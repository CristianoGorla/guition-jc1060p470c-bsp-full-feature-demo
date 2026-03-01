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

    // Temporaneamente disabilita i log ERROR per i2c.master
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    printf("\n");
    printf("\033[36m========================================\033[0m\n");
    printf("\033[36m   I2C DEVICES FOUND\033[0m\n");
    printf("\033[36m========================================\033[0m\n");
    printf("\n");

    int devices_found = 0;
    bool found_gt911_both = false;

    // Scan completo silenzioso
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,
        };

        i2c_master_dev_handle_t dev_handle;
        esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
        
        if (ret == ESP_OK) {
            uint8_t dummy;
            ret = i2c_master_receive(dev_handle, &dummy, 1, 10);
            i2c_master_bus_rm_device(dev_handle);
            
            if (ret == ESP_OK || ret == ESP_ERR_TIMEOUT) {
                // Skip 0x08 (falso positivo comune)
                if (addr == 0x08) {
                    continue;
                }
                
                devices_found++;
                
                // Trova nome
                const char *device_name = NULL;
                for (int i = 0; i < sizeof(known_devices) / sizeof(known_devices[0]); i++) {
                    if (known_devices[i].addr == addr) {
                        device_name = known_devices[i].name;
                        break;
                    }
                }
                
                if (device_name) {
                    printf("\033[32m[0x%02X] ✓ %s\033[0m\n", addr, device_name);
                    
                    // Verifica se GT911 risponde a entrambi gli indirizzi
                    if (addr == 0x14 || addr == 0x5D) {
                        static bool found_first_gt911 = false;
                        if (found_first_gt911) {
                            found_gt911_both = true;
                        }
                        found_first_gt911 = true;
                    }
                } else {
                    printf("\033[32m[0x%02X] ✓ Unknown device\033[0m\n", addr);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    printf("\n");
    printf("\033[36m========================================\033[0m\n");
    printf("\033[32mTotal devices: %d\033[0m\n", devices_found);
    printf("\033[36m========================================\033[0m\n");
    printf("\n");
    
    // Warnings
    if (found_gt911_both) {
        ESP_LOGW(TAG, "GT911 responds at BOTH 0x14 and 0x5D!");
        ESP_LOGW(TAG, "This may indicate incorrect reset sequence.");
        ESP_LOGW(TAG, "Only 0x14 should respond (INT=HIGH config).");
        printf("\n");
    }
    
    // Verifica dispositivi attesi
    ESP_LOGI(TAG, "Expected devices:");
    ESP_LOGI(TAG, "  ✓ 0x14 = GT911 Touch (board config)");
    ESP_LOGI(TAG, "  ✓ 0x18 = ES8311 Audio Codec");
    ESP_LOGI(TAG, "  ? 0x32 = RX8025T RTC (needs init)");
    printf("\n");
    
    // Riabilita i log i2c.master
    esp_log_level_set("i2c.master", ESP_LOG_ERROR);
}

bool i2c_check_bus_health(i2c_master_bus_handle_t bus_handle)
{
    if (!bus_handle) {
        ESP_LOGE(TAG, "Bus handle is NULL - cannot check health");
        return false;
    }

    ESP_LOGI(TAG, "\n========== I2C BUS HEALTH CHECK ==========");
    
    // Temporarily disable i2c.master error logs
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    // Test addresses for known devices on this board
    const uint8_t test_addrs[] = {0x14, 0x5D, 0x18, 0x32};  // GT911 both addresses, ES8311, RTC
    const char *test_names[] = {"GT911 (0x14)", "GT911 (0x5D)", "ES8311", "RX8025T"};
    const int num_tests = sizeof(test_addrs) / sizeof(test_addrs[0]);
    
    int successful_probes = 0;
    int failed_probes = 0;
    bool gt911_both_respond = false;
    
    for (int i = 0; i < num_tests; i++) {
        esp_err_t ret = i2c_master_probe(bus_handle, test_addrs[i], 500);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  [0x%02X] %s: ✓ RESPONDS", test_addrs[i], test_names[i]);
            successful_probes++;
            
            // Track if both GT911 addresses respond (means not initialized)
            if (i < 2) {  // First two are GT911 addresses
                static bool gt911_first = false;
                if (gt911_first) {
                    gt911_both_respond = true;
                }
                gt911_first = true;
            }
        } else {
            ESP_LOGW(TAG, "  [0x%02X] %s: ✗ NO RESPONSE (0x%x)", 
                     test_addrs[i], test_names[i], ret);
            failed_probes++;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Re-enable i2c.master logs
    esp_log_level_set("i2c.master", ESP_LOG_ERROR);
    
    ESP_LOGI(TAG, "\nHealth check summary:");
    ESP_LOGI(TAG, "  Successful probes: %d/%d", successful_probes, num_tests);
    ESP_LOGI(TAG, "  Failed probes: %d/%d", failed_probes, num_tests);
    
    if (gt911_both_respond) {
        ESP_LOGW(TAG, "  ⚠ GT911 responds to BOTH 0x14 and 0x5D");
        ESP_LOGW(TAG, "  This indicates GT911 is NOT initialized (good for I2C test)");
    }
    
    bool is_healthy = (successful_probes > 0);  // At least one device must respond
    
    if (is_healthy) {
        ESP_LOGI(TAG, "\n✓ I2C bus appears HEALTHY (%d devices responding)", successful_probes);
    } else {
        ESP_LOGE(TAG, "\n✗ I2C bus appears DEAD (no devices responding)");
        ESP_LOGE(TAG, "  Possible causes:");
        ESP_LOGE(TAG, "    1. Bus electrically shorted or stuck");
        ESP_LOGE(TAG, "    2. GPIO conflict with other peripheral");
        ESP_LOGE(TAG, "    3. Clock/power domain issue");
        ESP_LOGE(TAG, "    4. SDA stuck LOW (check with GPIO probe)");
    }
    
    ESP_LOGI(TAG, "==========================================\n");
    
    return is_healthy;
}
