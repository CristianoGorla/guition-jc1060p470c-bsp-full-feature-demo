#include "rtc_test.h"
#include "rtc_rx8025t.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RTC_TEST";

// Test addresses for RTC (0x32 is standard, but test alternates)
static const uint8_t test_addresses[] = {0x32, 0x30, 0x31, 0x33};

// Test speeds (from slowest to fastest)
static const uint32_t test_speeds[] = {
    10000,   // 10 kHz - ultra slow
    50000,   // 50 kHz - very slow
    100000,  // 100 kHz - standard
    200000,  // 200 kHz - fast
    400000   // 400 kHz - full speed
};

esp_err_t rtc_test_at_address(i2c_master_bus_handle_t bus_handle, uint8_t addr, uint32_t speed_hz)
{
    if (!bus_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Testing address 0x%02X at %lu Hz...", addr, speed_hz);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = speed_hz,
    };

    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "  Failed to add device (0x%x)", ret);
        return ret;
    }

    // Test 1: Simple receive (most gentle)
    uint8_t dummy;
    ret = i2c_master_receive(dev_handle, &dummy, 1, 1000);
    
    if (ret == ESP_OK || ret == ESP_ERR_TIMEOUT) {
        ESP_LOGI(TAG, "  ✓ Device responds to receive (0x%x)", ret);
        
        // Test 2: Try to read time registers
        uint8_t reg_addr = 0x00;  // Seconds register
        uint8_t time_data[7];
        ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, time_data, 7, 1000);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  ✓✓ SUCCESS! Can read time registers!");
            ESP_LOGI(TAG, "  Raw data: %02X %02X %02X %02X %02X %02X %02X",
                     time_data[0], time_data[1], time_data[2], time_data[3],
                     time_data[4], time_data[5], time_data[6]);
            
            i2c_master_bus_rm_device(dev_handle);
            return ESP_OK;
        } else {
            ESP_LOGW(TAG, "  ✗ Cannot read registers (0x%x)", ret);
        }
    } else {
        ESP_LOGW(TAG, "  ✗ No response (0x%x)", ret);
    }

    i2c_master_bus_rm_device(dev_handle);
    return ret;
}

esp_err_t rtc_test_read_only(i2c_master_bus_handle_t bus_handle)
{
    if (!bus_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "\n=== READ-ONLY TEST (no write) ===");
    ESP_LOGI(TAG, "Trying to read without any register write...");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x32,
        .scl_speed_hz = 100000,
    };

    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device");
        return ret;
    }

    // Try direct receive (no register address write)
    uint8_t data[16];
    ret = i2c_master_receive(dev_handle, data, sizeof(data), 1000);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ SUCCESS! RTC responds to direct read!");
        ESP_LOGI(TAG, "Data: %02X %02X %02X %02X %02X %02X %02X %02X",
                 data[0], data[1], data[2], data[3],
                 data[4], data[5], data[6], data[7]);
    } else {
        ESP_LOGE(TAG, "✗ Failed (0x%x)", ret);
    }

    i2c_master_bus_rm_device(dev_handle);
    return ret;
}

void rtc_test_speeds(i2c_master_bus_handle_t bus_handle)
{
    if (!bus_handle) {
        ESP_LOGE(TAG, "Invalid bus handle");
        return;
    }

    ESP_LOGI(TAG, "\n=== SPEED TEST ===");
    ESP_LOGI(TAG, "Testing RTC at different I2C speeds...");
    ESP_LOGI(TAG, "");

    for (int i = 0; i < sizeof(test_speeds) / sizeof(test_speeds[0]); i++) {
        uint32_t speed = test_speeds[i];
        ESP_LOGI(TAG, "\n--- Testing %lu Hz ---", speed);
        
        esp_err_t ret = rtc_test_at_address(bus_handle, 0x32, speed);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "\n✓✓✓ FOUND WORKING SPEED: %lu Hz ✓✓✓\n", speed);
            return;  // Stop at first working speed
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGE(TAG, "\n✗✗✗ No working speed found ✗✗✗\n");
}

void rtc_hardware_test(i2c_master_bus_handle_t bus_handle)
{
    if (!bus_handle) {
        ESP_LOGE(TAG, "Invalid bus handle");
        return;
    }

    ESP_LOGI(TAG, "\n");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   RTC HARDWARE DIAGNOSTIC TEST");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "This test will try:");
    ESP_LOGI(TAG, "  1. Different I2C addresses");
    ESP_LOGI(TAG, "  2. Different I2C speeds (10kHz - 400kHz)");
    ESP_LOGI(TAG, "  3. Read-only operations (no writes)");
    ESP_LOGI(TAG, "  4. Different timeout values");
    ESP_LOGI(TAG, "");

    // Test 1: Read-only (gentlest approach)
    esp_err_t ret = rtc_test_read_only(bus_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "\n✓✓✓ RTC FOUND with read-only test! ✓✓✓\n");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    // Test 2: Speed sweep at standard address
    rtc_test_speeds(bus_handle);
    
    vTaskDelay(pdMS_TO_TICKS(500));

    // Test 3: Try alternative addresses at 100kHz
    ESP_LOGI(TAG, "\n=== ADDRESS SCAN ===");
    ESP_LOGI(TAG, "Testing alternative I2C addresses at 100kHz...");
    ESP_LOGI(TAG, "");
    
    for (int i = 0; i < sizeof(test_addresses) / sizeof(test_addresses[0]); i++) {
        uint8_t addr = test_addresses[i];
        ESP_LOGI(TAG, "\n--- Testing address 0x%02X ---", addr);
        
        ret = rtc_test_at_address(bus_handle, addr, 100000);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "\n✓✓✓ FOUND RTC at address 0x%02X! ✓✓✓\n", addr);
            return;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Test 4: Ultra-slow scan (last resort)
    ESP_LOGI(TAG, "\n=== ULTRA-SLOW TEST ===");
    ESP_LOGI(TAG, "Final attempt: 10kHz with 2-second timeout...");
    ESP_LOGI(TAG, "");
    
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x32,
        .scl_speed_hz = 10000,  // 10kHz - extremely slow
    };

    i2c_master_dev_handle_t dev_handle;
    ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret == ESP_OK) {
        uint8_t reg_addr = 0x00;
        uint8_t time_data[7];
        ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, time_data, 7, 2000);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✓ Ultra-slow test succeeded!");
            ESP_LOGI(TAG, "Data: %02X %02X %02X %02X %02X %02X %02X",
                     time_data[0], time_data[1], time_data[2], time_data[3],
                     time_data[4], time_data[5], time_data[6]);
        } else {
            ESP_LOGE(TAG, "✗ Ultra-slow test failed (0x%x)", ret);
        }
        
        i2c_master_bus_rm_device(dev_handle);
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   DIAGNOSTIC TEST COMPLETE");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "\n⚠️  CONCLUSION: RTC does not respond");
        ESP_LOGE(TAG, "Possible causes:");
        ESP_LOGE(TAG, "  1. RTC chip not populated (check U9 on PCB)");
        ESP_LOGE(TAG, "  2. Missing VBAT (CR2032 battery)");
        ESP_LOGE(TAG, "  3. I2C bus issue after GT911 reset");
        ESP_LOGE(TAG, "  4. RTC in deep sleep mode");
        ESP_LOGE(TAG, "");
    }
}
