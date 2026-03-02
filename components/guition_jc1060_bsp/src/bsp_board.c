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

/* Hardware Pin Definitions (from Guition JC1060P470C V1.0 schematics) */
#define SD_POWER_EN_PIN         36  /* SD Card Power Enable (active HIGH) */
#define I2C_MASTER_SCL_IO       GPIO_NUM_8
#define I2C_MASTER_SDA_IO       GPIO_NUM_7  /* CORRECTED: was GPIO_NUM_3 */
#define I2C_MASTER_FREQ_HZ      400000

/* Timing Constants */
#define HARD_RESET_DISCHARGE_MS 500  /* Capacitor discharge time */
#define POWER_STABILIZATION_MS  100  /* Power rail stabilization delay */
#define SD_POWER_DELAY_MS        50  /* Delay after SD power on */

/* Global I2C bus handle (shared by all I2C peripherals) */
i2c_master_bus_handle_t g_i2c_bus_handle = NULL;

/**
 * @brief Check if hard reset is needed
 * 
 * Determines whether we need to perform a hard reset based on the reset reason.
 * 
 * Hard reset is needed for:
 * - ESP_RST_PANIC: System crash (need clean state)
 * - ESP_RST_INT_WDT / ESP_RST_TASK_WDT: Watchdog timeout (likely hung state)
 * - ESP_RST_BROWNOUT: Power glitch (uncertain hardware state)
 * 
 * Hard reset is NOT needed for:
 * - ESP_RST_POWERON: Already clean state
 * - ESP_RST_SW: Software reset via esp_restart()
 * - ESP_RST_USB_UART: IDF monitor restart (Ctrl+T, Ctrl+R)
 * - ESP_RST_DEEPSLEEP: Controlled wake from sleep
 * 
 * @return true if hard reset is needed, false otherwise
 */
static bool bsp_needs_hard_reset(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    
    switch (reason) {
        case ESP_RST_POWERON:
            ESP_LOGI(TAG, "[RESET] Cold boot (power-on reset) - no hard reset needed");
            return false;
            
        case ESP_RST_SW:
            ESP_LOGI(TAG, "[RESET] Software reset (esp_restart) - no hard reset needed");
            ESP_LOGI(TAG, "[RESET] ESP-Hosted will manage C6 reset during init_wifi()");
            return false;
            
        case 11:  // ESP_RST_USB_UART (IDF monitor Ctrl+T, Ctrl+R)
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
 * NOTE: 
 * - GPIO54 (C6 reset) is NOT managed by BSP - ESP-Hosted owns it exclusively.
 * - GPIO18 (SDIO CLK) is NOT managed by BSP - SDMMC driver owns it.
 * 
 * NOT performed on:
 * - Power-on reset (already clean)
 * - Software reset (esp_restart is clean)
 * - USB-UART reset (IDF monitor restart is clean)
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
        .pin_bit_mask = (1ULL << SD_POWER_EN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    /* Cut SD card power */
    gpio_set_level(SD_POWER_EN_PIN, 0);
    ESP_LOGI(TAG, "[RESET]   GPIO%d (SD_POWER_EN) → LOW", SD_POWER_EN_PIN);
    
    /* Wait for capacitor discharge */
    ESP_LOGI(TAG, "[RESET]   Waiting %dms for capacitor discharge...", HARD_RESET_DISCHARGE_MS);
    vTaskDelay(pdMS_TO_TICKS(HARD_RESET_DISCHARGE_MS));
    
    ESP_LOGI(TAG, "[RESET] Hard reset complete (SD card only)");
    ESP_LOGI(TAG, "[RESET] NOTE: C6 (GPIO54) and SDIO signals managed by drivers, not BSP");
}

/**
 * @brief Phase A: Power Manager Initialization
 * 
 * Implements deterministic power sequencing:
 * 1. Hard reset (if crash/watchdog/brownout) - SD card only
 * 2. Power isolation (SD unpowered)
 * 3. Controlled power-on (SD card)
 * 
 * NOTES:
 * - GPIO54 (C6 reset) is NEVER touched by BSP - ESP-Hosted owns it exclusively
 * - GPIO18 (SDIO CLK) is NEVER touched by BSP - SDMMC driver owns it
 * - C6 strapping (IO9) must be handled by hardware pull-ups, not software
 */
static esp_err_t bsp_phase_a_power_manager(void)
{
    ESP_LOGI(TAG, "[PHASE A] Power Manager starting...");
    
    /* Step 1: Hard reset only if needed (crash/watchdog/brownout) */
    bsp_hard_reset();
    
    /* Step 2: GPIO isolation (pre-initialization guard) */
    ESP_LOGI(TAG, "[PHASE A] Configuring SD card power control...");
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << SD_POWER_EN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    /* SD power (cut power) */
    gpio_set_level(SD_POWER_EN_PIN, 0);
    ESP_LOGI(TAG, "[PHASE A]   GPIO%d (SD_POWER_EN) → LOW (SD unpowered)", SD_POWER_EN_PIN);
    ESP_LOGI(TAG, "[PHASE A] NOTE: GPIO54 (C6) and GPIO18 (SDIO CLK) managed by drivers");
    
    /* Step 3: Wait for power rail stabilization */
    ESP_LOGI(TAG, "[PHASE A] Waiting %dms for rail stabilization...", POWER_STABILIZATION_MS);
    vTaskDelay(pdMS_TO_TICKS(POWER_STABILIZATION_MS));
    
    /* Step 4: Power-on sequence (SD card only) */
    ESP_LOGI(TAG, "[PHASE A] Power-on sequence starting...");
    
    /* SD card power ON */
    gpio_set_level(SD_POWER_EN_PIN, 1);
    ESP_LOGI(TAG, "[POWER]   GPIO%d (SD_POWER_EN) → HIGH (SD powered)", SD_POWER_EN_PIN);
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
             I2C_MASTER_SCL_IO, I2C_MASTER_SDA_IO, I2C_MASTER_FREQ_HZ);

    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
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
