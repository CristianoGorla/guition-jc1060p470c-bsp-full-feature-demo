/*
 * I2C Test Utilities - Targeted peripheral testing
 * Documented in: I2C_MIPI_DSI_CONFLICT.md
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "i2c_test.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "I2C_TEST";

/* Check GPIO state - uses Kconfig macros */
bool i2c_check_gpio_state(const char *context)
{
#ifdef CONFIG_DEBUG_I2C_GPIO_CHECK
    ESP_LOGI(TAG, "=== I2C GPIO STATE CHECK (%s) ===", context ? context : "manual");
    
    /* Reset GPIO to input with pullups */
    gpio_reset_pin(CONFIG_BSP_I2C_SDA_GPIO);
    gpio_reset_pin(CONFIG_BSP_I2C_SCL_GPIO);
    gpio_set_direction(CONFIG_BSP_I2C_SDA_GPIO, GPIO_MODE_INPUT);
    gpio_set_direction(CONFIG_BSP_I2C_SCL_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(CONFIG_BSP_I2C_SDA_GPIO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(CONFIG_BSP_I2C_SCL_GPIO, GPIO_PULLUP_ONLY);
    
    vTaskDelay(pdMS_TO_TICKS(10));
    
    int sda_level = gpio_get_level(CONFIG_BSP_I2C_SDA_GPIO);
    int scl_level = gpio_get_level(CONFIG_BSP_I2C_SCL_GPIO);
    
    ESP_LOGI(TAG, "GPIO%d (SDA) level: %d", CONFIG_BSP_I2C_SDA_GPIO, sda_level);
    ESP_LOGI(TAG, "GPIO%d (SCL) level: %d", CONFIG_BSP_I2C_SCL_GPIO, scl_level);
    
    bool healthy = (sda_level == 1 && scl_level == 1);
    
    if (healthy) {
        ESP_LOGI(TAG, "[OK] GPIO levels OK (both HIGH with pullups)");
    } else {
        ESP_LOGE(TAG, "[FAIL] GPIO FAULT DETECTED!");
        if (sda_level == 0) {
            ESP_LOGE(TAG, "  -> SDA (GPIO%d) stuck LOW", CONFIG_BSP_I2C_SDA_GPIO);
        }
        if (scl_level == 0) {
            ESP_LOGE(TAG, "  -> SCL (GPIO%d) stuck LOW", CONFIG_BSP_I2C_SCL_GPIO);
        }
    }
    
    return healthy;
#else
    return true;  /* Assume healthy if check disabled */
#endif
}

/* Re-initialize I2C bus - uses Kconfig macros */
esp_err_t i2c_reinit_bus(i2c_master_bus_handle_t *bus_handle)
{
#ifdef CONFIG_DEBUG_I2C_AUTO_RECOVERY
    ESP_LOGI(TAG, "=== I2C BUS RE-INITIALIZATION ===");
    
    /* Delete existing bus */
    if (*bus_handle != NULL) {
        ESP_LOGI(TAG, "Deleting existing I2C bus...");
        i2c_del_master_bus(*bus_handle);
        *bus_handle = NULL;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    /* Force GPIO reset */
    gpio_reset_pin(CONFIG_BSP_I2C_SDA_GPIO);
    gpio_reset_pin(CONFIG_BSP_I2C_SCL_GPIO);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    /* Re-create bus with Kconfig settings */
    ESP_LOGI(TAG, "Creating new I2C bus (SDA=%d, SCL=%d)...", 
             CONFIG_BSP_I2C_SDA_GPIO, CONFIG_BSP_I2C_SCL_GPIO);
    
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_BSP_I2C_SCL_GPIO,
        .sda_io_num = CONFIG_BSP_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, bus_handle);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[OK] I2C bus re-initialized successfully");
    } else {
        ESP_LOGE(TAG, "[FAIL] Failed to re-initialize I2C bus: %s", esp_err_to_name(ret));
    }
    
    return ret;
#else
    (void)bus_handle;  /* Unused parameter */
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

/* Test peripherals - targeted probing only */
void i2c_test_peripherals(i2c_master_bus_handle_t bus_handle)
{
#ifdef CONFIG_DEBUG_I2C_TEST_PERIPHERALS
    if (!bus_handle) {
        ESP_LOGE(TAG, "Invalid I2C bus handle");
        return;
    }

    ESP_LOGI(TAG, "========== I2C PERIPHERAL TEST ==========");
    int devices_found = 0;
    esp_err_t ret;
    
#ifdef CONFIG_BSP_ENABLE_TOUCH
    ret = i2c_master_probe(bus_handle, 0x14, 100);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[0x14] [OK] GT911 Touch");
        devices_found++;
    } else {
        ESP_LOGE(TAG, "[0x14] [FAIL] GT911 Touch NOT responding");
    }
#endif

#ifdef CONFIG_BSP_ENABLE_AUDIO
    ret = i2c_master_probe(bus_handle, 0x18, 100);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[0x18] [OK] ES8311 Audio Codec");
        devices_found++;
    } else {
        ESP_LOGE(TAG, "[0x18] [FAIL] ES8311 Audio NOT responding");
    }
#endif

#ifdef CONFIG_BSP_ENABLE_RTC
    ret = i2c_master_probe(bus_handle, 0x32, 100);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[0x32] [OK] RX8025T RTC");
        devices_found++;
    } else {
        ESP_LOGE(TAG, "[0x32] [FAIL] RX8025T RTC NOT responding");
    }
#endif

    ESP_LOGI(TAG, "Total devices: %d", devices_found);
    ESP_LOGI(TAG, "=========================================");
#else
    (void)bus_handle;  /* Unused parameter */
#endif
}
