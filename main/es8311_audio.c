#include "es8311_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ES8311";

// Power Amplifier control pin (from GUITION board schematic)
#define ES8311_PA_POWER_PIN GPIO_NUM_11

static esp_err_t es8311_write_reg(i2c_master_bus_handle_t bus_handle, uint8_t reg_addr, uint8_t data)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device (0x%x)", ret);
        return ret;
    }

    uint8_t write_buf[2] = {reg_addr, data};
    ret = i2c_master_transmit(dev_handle, write_buf, 2, 1000);
    
    i2c_master_bus_rm_device(dev_handle);
    return ret;
}

static esp_err_t es8311_read_reg(i2c_master_bus_handle_t bus_handle, uint8_t reg_addr, uint8_t *data)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device (0x%x)", ret);
        return ret;
    }

    ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, 1, 1000);
    
    i2c_master_bus_rm_device(dev_handle);
    return ret;
}

esp_err_t es8311_read_chip_id(i2c_master_bus_handle_t bus_handle, uint8_t *chip_id)
{
    if (!bus_handle || !chip_id) {
        return ESP_ERR_INVALID_ARG;
    }

    return es8311_read_reg(bus_handle, ES8311_CHIP_ID_REG, chip_id);
}

esp_err_t es8311_init(i2c_master_bus_handle_t bus_handle)
{
    if (!bus_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing ES8311 audio codec...");
    ESP_LOGI(TAG, "I2C Address: 0x%02X (direct init, no pre-probe)", ES8311_I2C_ADDR);

    // Direct init - validate presence by reading chip ID
    uint8_t chip_id = 0;
    esp_err_t ret = es8311_read_chip_id(bus_handle, &chip_id);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID (0x%x)", ret);
        ESP_LOGW(TAG, "ES8311 not responding (may not be powered or populated)");
        return ret;
    }

    ESP_LOGI(TAG, "✓ ES8311 responding on I2C!");
    ESP_LOGI(TAG, "ES8311 Chip ID: 0x%02X (expected: 0x83)", chip_id);

    if (chip_id != 0x83) {
        ESP_LOGW(TAG, "Unexpected chip ID (expected 0x83, got 0x%02X)", chip_id);
    }

    // Soft reset codec to clear any stuck state
    ESP_LOGI(TAG, "Performing soft reset...");
    ret = es8311_write_reg(bus_handle, ES8311_RESET_REG, 0x1F);  // Reset all
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write reset register (0x%x)", ret);
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(50));  // Wait for reset

    ret = es8311_write_reg(bus_handle, ES8311_RESET_REG, 0x00);  // Clear reset
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear reset register (0x%x)", ret);
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    // Set codec to power-down mode (safe state, releases I2C bus)
    ESP_LOGI(TAG, "Setting codec to power-down mode...");
    ret = es8311_write_reg(bus_handle, ES8311_SYSTEM_REG, 0x00);  // Power down
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to write system register (0x%x)", ret);
    }

    ESP_LOGI(TAG, "✓ ES8311 initialized successfully (powered down, safe state)");
    ESP_LOGI(TAG, "Note: PA power pin GPIO%d not configured (needs I2S setup)", ES8311_PA_POWER_PIN);
    
    return ESP_OK;
}
