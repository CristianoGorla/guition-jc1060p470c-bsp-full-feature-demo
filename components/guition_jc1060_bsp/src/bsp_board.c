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

static const char *TAG = "BSP";

/* Hardware configuration from Kconfig (with fallback defaults) */
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

#define SD_POWER_DELAY_MS 50  /* Delay after SD power on */

/* Global I2C bus handle (shared by all I2C peripherals) */
i2c_master_bus_handle_t g_i2c_bus_handle = NULL;

/**
 * @brief Check if hard reset is needed
 * 
 * Determines whether we need to perform a hard reset based on the reset reason.
 * 
 * === HARD RESET IS NEEDED FOR ===
 * These reset reasons indicate uncertain hardware state that requires power cycling:
 * 
 * - ESP_RST_PANIC (0x03): System crash/exception
 *   Hardware may be in inconsistent state, peripherals may be hung
 *   → Need complete power cycle to ensure clean state
 * 
 * - ESP_RST_INT_WDT / ESP_RST_TASK_WDT / ESP_RST_WDT (0x05-0x06):
 *   Watchdog timeout indicates software hang, hardware may be stuck
 *   → Need power cycle to break any hardware deadlock
 * 
 * - ESP_RST_BROWNOUT (0x04): Power supply glitch/undervoltage
 *   Hardware state is uncertain after power instability
 *   → Need controlled power cycle to re-establish stable state
 * 
 * === HARD RESET IS NOT NEEDED FOR ===
 * These reset reasons guarantee clean hardware state:
 * 
 * - ESP_RST_POWERON (0x01): Cold boot / USB cable reconnection
 *   Hardware is already in clean power-on state
 *   Boot ROM → Bootloader → App is deterministic
 *   Test case: Disconnect/reconnect USB cable
 *   Log: "[RESET] Cold boot (power-on reset) - no hard reset needed"
 * 
 * - ESP_RST_SW (0x0C): Software reset via esp_restart()
 *   Clean software-initiated reset, hardware state is controlled
 *   ROM bootloader re-initializes all peripherals
 *   Test case: Call esp_restart() in code
 *   Log: "[RESET] Software reset (esp_restart) - no hard reset needed"
 * 
 * - ESP_RST_USB_UART (0x0B / 11): VSCode IDF Monitor reset (Ctrl+T, Ctrl+R)
 *   Clean reset triggered by VSCode ESP-IDF extension
 *   USB-JTAG bridge resets the chip via RESET pin
 *   Hardware state is deterministic (same as power-on)
 *   Test case: Press Ctrl+T then Ctrl+R in VSCode terminal
 *   Log: "[RESET] USB-UART reset (IDF monitor) - no hard reset needed"
 *   NOTE: This is a CHIP reset, not just a USB disconnect!
 * 
 * - ESP_RST_DEEPSLEEP (0x05): Wake from deep sleep
 *   Controlled wake from low-power mode, RTC domain preserved
 *   Peripherals were cleanly shut down before sleep
 *   Test case: Enter deep sleep and wake via timer/GPIO
 *   Log: "[RESET] Wake from deep sleep - no hard reset needed"
 * 
 * === HARDWARE OWNERSHIP NOTES ===
 * BSP Phase A manages ONLY the SD card power domain:
 * - GPIO36 (SD_POWER_EN): Controlled by BSP for hard reset
 * 
 * Other hardware is owned by drivers and NOT reset by BSP:
 * - GPIO54 (C6_RESET): Owned exclusively by ESP-Hosted driver
 * - GPIO18 (SDIO_CLK): Owned by SDMMC host driver
 * - C6 strapping (IO9): Hardware pull-ups, not software controlled
 * 
 * === RESET SEQUENCE TIMING ===
 * Hard reset (when needed):
 * 1. Cut SD card power (GPIO36 LOW)
 * 2. Wait CONFIG_BSP_HARD_RESET_DISCHARGE_MS (default 500ms) for capacitor discharge
 * 3. Power stabilization happens in Phase A main sequence
 * 
 * Normal boot (no hard reset):
 * 1. Isolate SD card (GPIO36 LOW)
 * 2. Wait CONFIG_BSP_POWER_STABILIZATION_MS (default 100ms)
 * 3. Power on SD card (GPIO36 HIGH)
 * 4. Wait SD_POWER_DELAY_MS (50ms) before SDMMC operations
 * 
 * @return true if hard reset is needed, false otherwise
 */
static bool bsp_needs_hard_reset(void)
{
#ifndef CONFIG_BSP_ENABLE_HARD_RESET
    return false;  /* Hard reset disabled in Kconfig */
#endif

    esp_reset_reason_t reason = esp_reset_reason();
    
    switch (reason) {
        case ESP_RST_POWERON:
            ESP_LOGI(TAG, "[RESET] Cold boot (power-on reset) - no hard reset needed");
            return false;
            
        case ESP_RST_SW:
            ESP_LOGI(TAG, "[RESET] Software reset (esp_restart) - no hard reset needed");
            ESP_LOGI(TAG, "[RESET] ESP-Hosted will manage C6 reset during init_wifi()");
            return false;
            
        case 11:  // ESP_RST_USB_UART (VSCode IDF Monitor Ctrl+T, Ctrl+R)
            ESP_LOGI(TAG, "[RESET] USB-UART reset (IDF monitor) - no hard reset needed");
            ESP_LOGI(TAG, "[RESET] ESP-Hosted will manage C6 reset during init_wifi()");
            return false;
            
        case ESP_RST_DEEPSLEEP:
            ESP_LOGI(TAG, "[RESET] Wake from deep sleep - no hard reset needed");
            return false;
            
        case ESP_RST_PANIC:
            ESP_LOGW(TAG, "[RESET] System crash detected (panic) - hard reset required");
            return true;
            
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            ESP_LOGW(TAG, "[RESET] Watchdog timeout detected - hard reset required");
            return true;
            
        case ESP_RST_BROWNOUT:
            ESP_LOGW(TAG, "[RESET] Brownout detected - hard reset required");
            return true;
            
        default:
            ESP_LOGW(TAG, "[RESET] Unknown reset reason (%d) - performing hard reset for safety", reason);
            return true;
    }
}

/**
 * @brief Perform deterministic hard reset
 * 
 * Forces complete power-down of SD Card to ensure clean state after
 * crashes, watchdog timeouts, or power glitches.
 * 
 * SCOPE: SD Card power domain only (GPIO36)
 * NOT managed: C6 reset (GPIO54), SDIO CLK (GPIO18) - owned by drivers
 * 
 * NOT performed on:
 * - Power-on reset (already clean)
 * - Software reset (esp_restart is clean)
 * - USB-UART reset (VSCode IDF monitor is clean)
 * - Deep sleep wake (controlled wake)
 */
static void bsp_hard_reset(void)
{
    if (!bsp_needs_hard_reset()) {
        return;  // No hard reset needed
    }
    
    ESP_LOGW(TAG, "[RESET] === HARD RESET CYCLE ===");
    
    /* Configure SD power control pin as output */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BSP_PIN_SD_POWER_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    /* Cut SD card power */
    gpio_set_level(CONFIG_BSP_PIN_SD_POWER_EN, 0);
    ESP_LOGI(TAG, "[RESET]   GPIO%d (SD_POWER_EN) → LOW", CONFIG_BSP_PIN_SD_POWER_EN);
    
    /* Wait for capacitor discharge */
    ESP_LOGI(TAG, "[RESET]   Waiting %dms for capacitor discharge...", CONFIG_BSP_HARD_RESET_DISCHARGE_MS);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_BSP_HARD_RESET_DISCHARGE_MS));
    
    ESP_LOGI(TAG, "[RESET] Hard reset complete (SD card only)");
    ESP_LOGI(TAG, "[RESET] NOTE: C6 (GPIO%d) and SDIO signals managed by drivers, not BSP", CONFIG_BSP_PIN_WIFI_RESET);
}

/**
 * @brief Phase A: Power Manager Initialization
 * 
 * Implements deterministic power sequencing:
 * 1. Hard reset (if crash/watchdog/brownout) - SD card only
 * 2. Power isolation (SD unpowered)
 * 3. Controlled power-on (SD card)
 * 
 * HARDWARE OWNERSHIP:
 * - BSP manages: GPIO36 (SD card power)
 * - ESP-Hosted manages: GPIO54 (C6 reset), GPIO18 (SDIO CLK)
 * - Hardware manages: C6 strapping (IO9 pull-ups)
 * 
 * RESET BEHAVIOR:
 * - Cold boot (USB reconnect): No hard reset, direct to power sequence
 * - VSCode reset (Ctrl+T, R): No hard reset, clean chip reset
 * - Software reset: No hard reset, ROM bootloader handles cleanup
 * - Crash/watchdog: Hard reset with 500ms capacitor discharge
 */
static esp_err_t bsp_phase_a_power_manager(void)
{
    ESP_LOGI(TAG, "[PHASE A] Power Manager starting...");
    
    /* Step 1: Hard reset only if needed (crash/watchdog/brownout) */
    bsp_hard_reset();
    
    /* Step 2: GPIO isolation (pre-initialization guard) */
    ESP_LOGI(TAG, "[PHASE A] Configuring SD card power control...");
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BSP_PIN_SD_POWER_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    /* SD power (cut power) */
    gpio_set_level(CONFIG_BSP_PIN_SD_POWER_EN, 0);
    ESP_LOGI(TAG, "[PHASE A]   GPIO%d (SD_POWER_EN) → LOW (SD unpowered)", CONFIG_BSP_PIN_SD_POWER_EN);
    ESP_LOGI(TAG, "[PHASE A] NOTE: GPIO%d (C6) and GPIO%d (SDIO CLK) managed by drivers", 
             CONFIG_BSP_PIN_WIFI_RESET, CONFIG_BSP_PIN_WIFI_SDIO_CLK);
    
    /* Step 3: Wait for power rail stabilization */
    ESP_LOGI(TAG, "[PHASE A] Waiting %dms for rail stabilization...", CONFIG_BSP_POWER_STABILIZATION_MS);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_BSP_POWER_STABILIZATION_MS));
    
    /* Step 4: Power-on sequence (SD card only) */
    ESP_LOGI(TAG, "[PHASE A] Power-on sequence starting...");
    
    /* SD card power ON */
    gpio_set_level(CONFIG_BSP_PIN_SD_POWER_EN, 1);
    ESP_LOGI(TAG, "[POWER]   GPIO%d (SD_POWER_EN) → HIGH (SD powered)", CONFIG_BSP_PIN_SD_POWER_EN);
    vTaskDelay(pdMS_TO_TICKS(SD_POWER_DELAY_MS));
    
    ESP_LOGI(TAG, "[POWER] SD card powered, rails stabilized");
    ESP_LOGI(TAG, "[POWER] C6 reset and SDIO signals will be managed by ESP-Hosted/SDMMC");
    
    ESP_LOGI(TAG, "[PHASE A] ✓ POWER_READY");
    
    return ESP_OK;
}

/**
 * @brief Initialize I2C bus (shared by touch, audio codec, RTC)
 */
static esp_err_t bsp_i2c_bus_init(void)
{
#if defined(CONFIG_BSP_ENABLE_TOUCH) || defined(CONFIG_BSP_ENABLE_AUDIO) || defined(CONFIG_BSP_ENABLE_RTC)
    ESP_LOGI(TAG, "[I2C] Initializing I2C bus (SCL=%d, SDA=%d, %d Hz)",
             CONFIG_BSP_I2C_SCL_GPIO, CONFIG_BSP_I2C_SDA_GPIO, CONFIG_BSP_I2C_FREQ_HZ);

    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_BSP_I2C_SCL_GPIO,
        .sda_io_num = CONFIG_BSP_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &g_i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[I2C] Failed to initialize I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "[I2C] ✓ Bus initialized");
#else
    ESP_LOGI(TAG, "[I2C] Skipped (no I2C peripherals enabled in menuconfig)");
#endif
    return ESP_OK;
}

/**
 * @brief Phase D: Initialize peripheral drivers
 */
static esp_err_t bsp_phase_d_peripheral_drivers(void)
{
    ESP_LOGI(TAG, "[PHASE D] Peripheral Drivers initialization...");

    /* Initialize I2C bus first (shared by multiple peripherals) */
    esp_err_t ret = bsp_i2c_bus_init();
    if (ret != ESP_OK) {
        return ret;
    }

#ifdef CONFIG_BSP_ENABLE_DISPLAY
    ESP_LOGI(TAG, "[PHASE D] Initializing display...");
    esp_lcd_panel_handle_t display = bsp_display_init();
    if (display == NULL) {
        ESP_LOGE(TAG, "[PHASE D] Display initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[PHASE D] ✓ Display ready");
#endif

#ifdef CONFIG_BSP_ENABLE_TOUCH
    ESP_LOGI(TAG, "[PHASE D] Initializing touch controller...");
    esp_lcd_touch_handle_t touch = bsp_touch_init();
    if (touch == NULL) {
        ESP_LOGE(TAG, "[PHASE D] Touch initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[PHASE D] ✓ Touch ready");
#endif

#ifdef CONFIG_BSP_ENABLE_AUDIO
    ESP_LOGI(TAG, "[PHASE D] Initializing audio system...");
    bsp_audio_config_t audio_cfg = BSP_AUDIO_DEFAULT_CONFIG();
    ret = bsp_audio_init(&audio_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[PHASE D] Audio initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[PHASE D] ✓ Audio ready");
#endif

#ifdef CONFIG_BSP_ENABLE_RTC
    ESP_LOGI(TAG, "[PHASE D] Initializing RTC...");
    ret = bsp_rtc_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[PHASE D] RTC initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[PHASE D] ✓ RTC ready");
#endif

    ESP_LOGI(TAG, "[PHASE D] ✓ All enabled peripherals initialized");
    return ESP_OK;
}

/**
 * @brief Initialize Board Support Package
 * 
 * Entry point for BSP initialization. Implements:
 * - Phase A: Power Manager (always)
 * - Phase D: Peripheral Drivers (conditional on Kconfig)
 */
esp_err_t bsp_board_init(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Guition BSP v1.2.0-dev");
    ESP_LOGI(TAG, "  Phase A: Power Manager");
    ESP_LOGI(TAG, "  Phase D: Peripheral Drivers");
    ESP_LOGI(TAG, "========================================");
    
    /* Phase A: Power Manager (always executed) */
    esp_err_t ret = bsp_phase_a_power_manager();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase A failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Phase D: Peripheral Drivers (conditional on Kconfig) */
    ret = bsp_phase_d_peripheral_drivers();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase D failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  BSP Initialization Complete");
    ESP_LOGI(TAG, "========================================");
    
    return ESP_OK;
}

/**
 * @brief Deinitialize Board Support Package
 */
void bsp_board_deinit(void)
{
    if (g_i2c_bus_handle != NULL) {
        i2c_del_master_bus(g_i2c_bus_handle);
        g_i2c_bus_handle = NULL;
    }
    ESP_LOGI(TAG, "BSP deinitialized");
}
