/*
 * Guition JC1060P470C Board Support Package - Implementation
 * Phase A: Power Manager + Phase D: Peripheral Drivers
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "bsp_board.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "sdkconfig.h"

/* Include driver headers */
#ifdef CONFIG_BSP_ENABLE_DISPLAY
#include "../drivers/jd9165_bsp.h"
#endif
#ifdef CONFIG_BSP_ENABLE_TOUCH
#include "../drivers/gt911_bsp.h"
#endif
#ifdef CONFIG_BSP_ENABLE_AUDIO
#include "../drivers/es8311_bsp.h"
#endif
#ifdef CONFIG_BSP_ENABLE_RTC
#include "../drivers/rx8025t_bsp.h"
#endif

/* Include I2C test utilities */
#if defined(CONFIG_DEBUG_I2C_GPIO_CHECK) || defined(CONFIG_DEBUG_I2C_TEST_PERIPHERALS) || defined(CONFIG_DEBUG_I2C_AUTO_RECOVERY)
#include "../utils/i2c_test.h"
#endif

static const char *TAG = "BSP";

#ifndef CONFIG_BSP_PIN_SD_POWER_EN
#define CONFIG_BSP_PIN_SD_POWER_EN 36
#endif

#ifndef CONFIG_BSP_I2C_SCL_GPIO
#define CONFIG_BSP_I2C_SCL_GPIO 8
#endif

#ifndef CONFIG_BSP_I2C_SDA_GPIO
#define CONFIG_BSP_I2C_SDA_GPIO 7
#endif

#ifndef CONFIG_BSP_I2C_FREQ_HZ
#define CONFIG_BSP_I2C_FREQ_HZ 400000
#endif

#ifndef CONFIG_BSP_HARD_RESET_DISCHARGE_MS
#define CONFIG_BSP_HARD_RESET_DISCHARGE_MS 500
#endif

#ifndef CONFIG_BSP_POWER_STABILIZATION_MS
#define CONFIG_BSP_POWER_STABILIZATION_MS 100
#endif

#define SD_POWER_DELAY_MS 50

i2c_master_bus_handle_t g_i2c_bus_handle = NULL;

/* Hardware handles */
static esp_lcd_panel_handle_t g_display_handle = NULL;
static esp_lcd_touch_handle_t g_touch_handle = NULL;

static bool bsp_needs_hard_reset(void)
{
#ifndef CONFIG_BSP_ENABLE_HARD_RESET
    return false;
#endif

    esp_reset_reason_t reason = esp_reset_reason();
    
    switch (reason) {
        case ESP_RST_POWERON:
        case ESP_RST_SW:
        case 11:
        case ESP_RST_DEEPSLEEP:
            ESP_LOGI(TAG, "[RESET] Clean boot - no hard reset");
            return false;
        default:
            ESP_LOGW(TAG, "[RESET] Unsafe reset - hard reset required");
            return true;
    }
}

static void bsp_hard_reset(void)
{
    if (!bsp_needs_hard_reset()) return;
    
    ESP_LOGW(TAG, "[RESET] === HARD RESET ===");
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BSP_PIN_SD_POWER_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(CONFIG_BSP_PIN_SD_POWER_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_BSP_HARD_RESET_DISCHARGE_MS));
}

static esp_err_t bsp_phase_a_power_manager(void)
{
    ESP_LOGI(TAG, "[PHASE A] Power Manager...");
    bsp_hard_reset();
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BSP_PIN_SD_POWER_EN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
    gpio_set_level(CONFIG_BSP_PIN_SD_POWER_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_BSP_POWER_STABILIZATION_MS));
    gpio_set_level(CONFIG_BSP_PIN_SD_POWER_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(SD_POWER_DELAY_MS));
    
    ESP_LOGI(TAG, "[PHASE A] ✓ POWER_READY");
    return ESP_OK;
}

static esp_err_t bsp_i2c_bus_init(void)
{
#if defined(CONFIG_BSP_ENABLE_TOUCH) || defined(CONFIG_BSP_ENABLE_AUDIO) || defined(CONFIG_BSP_ENABLE_RTC)
    /* Skip if already initialized */
    if (g_i2c_bus_handle != NULL) {
        ESP_LOGI(TAG, "[I2C] Already initialized");
        return ESP_OK;
    }
    
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_BSP_I2C_SCL_GPIO,
        .sda_io_num = CONFIG_BSP_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &g_i2c_bus_handle));
    
    /* Enable verbose I2C logging if requested */
#ifdef CONFIG_DEBUG_I2C_VERBOSE
    esp_log_level_set("i2c", ESP_LOG_VERBOSE);
    esp_log_level_set("i2c.master", ESP_LOG_VERBOSE);
    ESP_LOGI(TAG, "[I2C] Verbose logging enabled");
#endif
    
    ESP_LOGI(TAG, "[I2C] ✓ Ready");
    
    /* Test peripherals after I2C init (if enabled) */
#ifdef CONFIG_DEBUG_I2C_TEST_PERIPHERALS
    i2c_test_peripherals(g_i2c_bus_handle);
#endif

#endif
    return ESP_OK;
}

static esp_err_t bsp_phase_d_peripheral_drivers(void)
{
    ESP_LOGI(TAG, "[PHASE D] Peripheral Drivers...");

    /* 
     * CRITICAL: Initialize display FIRST, then I2C
     * 
     * The JD9165 MIPI-DSI display controller corrupts the I2C peripheral 
     * hardware during initialization. The solution is to initialize I2C 
     * AFTER the display, not before.
     * 
     * This approach matches the Espressif vendor BSP:
     * - bsp_display_new() - no I2C
     * - bsp_touch_new() - calls bsp_i2c_init()
     * 
     * See: docs/I2C_MIPI_DSI_CONFLICT.md
     */
#ifdef CONFIG_BSP_ENABLE_DISPLAY
    g_display_handle = bsp_display_init();
    if (!g_display_handle) return ESP_FAIL;
    ESP_LOGI(TAG, "[PHASE D] ✓ Display HW");
#endif

    /* NOW initialize I2C after display is stable */
    ESP_ERROR_CHECK(bsp_i2c_bus_init());

#ifdef CONFIG_BSP_ENABLE_TOUCH
    g_touch_handle = bsp_touch_init();
    if (!g_touch_handle) return ESP_FAIL;
    ESP_LOGI(TAG, "[PHASE D] ✓ Touch HW");
#endif

#ifdef CONFIG_BSP_ENABLE_AUDIO
    bsp_audio_config_t audio_cfg = BSP_AUDIO_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(bsp_audio_init(&audio_cfg));
    ESP_LOGI(TAG, "[PHASE D] ✓ Audio");
#endif

#ifdef CONFIG_BSP_ENABLE_RTC
    ESP_ERROR_CHECK(bsp_rtc_init());
    ESP_LOGI(TAG, "[PHASE D] ✓ RTC");
#endif

    ESP_LOGI(TAG, "[PHASE D] ✓ Complete");
    return ESP_OK;
}

esp_err_t bsp_board_init(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Guition BSP v1.3.0");
    ESP_LOGI(TAG, "  Hardware Layer Only");
    ESP_LOGI(TAG, "========================================");
    
    ESP_ERROR_CHECK(bsp_phase_a_power_manager());
    ESP_ERROR_CHECK(bsp_phase_d_peripheral_drivers());
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ✓ BSP Ready (Hardware only)");
    ESP_LOGI(TAG, "  App must init LVGL separately");
    ESP_LOGI(TAG, "========================================");
    
    return ESP_OK;
}

esp_lcd_panel_handle_t bsp_display_get_handle(void)
{
    return g_display_handle;
}

esp_lcd_touch_handle_t bsp_touch_get_handle(void)
{
    return g_touch_handle;
}

i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void)
{
    return g_i2c_bus_handle;
}

void bsp_board_deinit(void)
{
    if (g_i2c_bus_handle) {
        i2c_del_master_bus(g_i2c_bus_handle);
        g_i2c_bus_handle = NULL;
    }
}
