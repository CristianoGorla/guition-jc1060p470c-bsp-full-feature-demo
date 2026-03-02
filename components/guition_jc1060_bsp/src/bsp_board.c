/*
 * Guition JC1060P470C Board Support Package - Implementation
 * Phase A: Power Manager with GPIO 18 Strapping Guard
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "bsp_board.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"

static const char *TAG = "BSP";

/* Hardware Pin Definitions (from Guition JC1060P470C V1.0 schematics) */
#define C6_CHIP_PU_PIN          54  /* ESP32-C6 Chip Power/Reset (active HIGH) */
#define SD_POWER_EN_PIN         36  /* SD Card Power Enable (active HIGH) */
#define C6_IO9_STRAP_PIN        18  /* ESP32-C6 IO9 Strapping (shared with SDIO CLK Slot 1) */
#define C6_READY_PIN             6  /* ESP32-C6 Ready Signal (input, future use) */

/* Timing Constants */
#define HARD_RESET_DISCHARGE_MS 500  /* Capacitor discharge time */
#define POWER_STABILIZATION_MS  100  /* Power rail stabilization delay */
#define C6_BOOT_DELAY_MS         50  /* Delay after C6 release */

/**
 * @brief Perform deterministic hard reset
 * 
 * Forces complete power-down of ESP32-C6 and SD Card to ensure
 * clean state on warm boots (watchdog resets, software resets, etc.)
 */
static void bsp_hard_reset(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    
    /* Check if warm boot (any reset except power-on) */
    if (reason != ESP_RST_POWERON) {
        ESP_LOGW(TAG, "[RESET] Warm boot detected (reason: %d), forcing hard reset", reason);
        ESP_LOGI(TAG, "[RESET] === HARD RESET CYCLE ===");
        
        /* Configure power control pins as outputs */
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        
        /* Force C6 into reset */
        io_conf.pin_bit_mask = (1ULL << C6_CHIP_PU_PIN);
        gpio_config(&io_conf);
        gpio_set_level(C6_CHIP_PU_PIN, 0);
        ESP_LOGI(TAG, "[RESET]   GPIO%d (C6_CHIP_PU) → LOW", C6_CHIP_PU_PIN);
        
        /* Cut SD card power */
        io_conf.pin_bit_mask = (1ULL << SD_POWER_EN_PIN);
        gpio_config(&io_conf);
        gpio_set_level(SD_POWER_EN_PIN, 0);
        ESP_LOGI(TAG, "[RESET]   GPIO%d (SD_POWER_EN) → LOW", SD_POWER_EN_PIN);
        
        /* Wait for capacitor discharge */
        ESP_LOGI(TAG, "[RESET]   Waiting %dms for capacitor discharge...", HARD_RESET_DISCHARGE_MS);
        vTaskDelay(pdMS_TO_TICKS(HARD_RESET_DISCHARGE_MS));
        
        ESP_LOGI(TAG, "[RESET] Hard reset complete");
    } else {
        ESP_LOGI(TAG, "[RESET] Cold boot detected (power-on reset)");
    }
}

/**
 * @brief Configure GPIO 18 strapping guard
 * 
 * CRITICAL: GPIO 18 is shared between:
 * - ESP32-P4 SDIO Slot 1 CLK
 * - ESP32-C6 IO9 (boot mode strapping pin)
 * 
 * The C6 reads IO9 at boot to determine boot mode:
 * - HIGH → SPI Boot (desired)
 * - LOW → Download Boot (undesired)
 * 
 * This function ensures GPIO 18 is forced HIGH before C6 reset release.
 */
static void bsp_strapping_guard(void)
{
    ESP_LOGI(TAG, "[STRAP] Configuring GPIO 18 strapping guard...");
    
    /* 
     * De-mux GPIO 18 from SDIO function (if active)
     * Force it as standard GPIO output
     */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << C6_IO9_STRAP_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    gpio_config(&io_conf);
    
    /* Force HIGH - ensures C6 boots in SPI mode */
    gpio_set_level(C6_IO9_STRAP_PIN, 1);
    
    ESP_LOGI(TAG, "[STRAP] GPIO%d (C6_IO9) → HIGH (SPI boot mode)", C6_IO9_STRAP_PIN);
    ESP_LOGI(TAG, "[STRAP] ✓ Strapping guard active");
}

/**
 * @brief Phase A: Power Manager Initialization
 * 
 * Implements deterministic power sequencing:
 * 1. Hard reset (if warm boot)
 * 2. Power isolation (C6 in reset, SD unpowered)
 * 3. GPIO 18 strapping guard
 * 4. Controlled power-on (SD first, then C6)
 */
static esp_err_t bsp_phase_a_power_manager(void)
{
    ESP_LOGI(TAG, "[PHASE A] Power Manager starting...");
    
    /* Step 1: Hard reset on warm boot */
    bsp_hard_reset();
    
    /* Step 2: GPIO isolation (pre-initialization guard) */
    ESP_LOGI(TAG, "[PHASE A] Forcing GPIO isolation...");
    
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    /* C6 reset (hold in reset) */
    io_conf.pin_bit_mask = (1ULL << C6_CHIP_PU_PIN);
    gpio_config(&io_conf);
    gpio_set_level(C6_CHIP_PU_PIN, 0);
    ESP_LOGI(TAG, "[PHASE A]   GPIO%d (C6_CHIP_PU) → LOW (C6 in reset)", C6_CHIP_PU_PIN);
    
    /* SD power (cut power) */
    io_conf.pin_bit_mask = (1ULL << SD_POWER_EN_PIN);
    gpio_config(&io_conf);
    gpio_set_level(SD_POWER_EN_PIN, 0);
    ESP_LOGI(TAG, "[PHASE A]   GPIO%d (SD_POWER_EN) → LOW (SD unpowered)", SD_POWER_EN_PIN);
    
    /* Step 3: GPIO 18 strapping guard (CRITICAL for C6 boot) */
    bsp_strapping_guard();
    
    /* Step 4: Wait for power rail stabilization */
    ESP_LOGI(TAG, "[PHASE A] Waiting %dms for rail stabilization...", POWER_STABILIZATION_MS);
    vTaskDelay(pdMS_TO_TICKS(POWER_STABILIZATION_MS));
    
    /* Step 5: Power-on sequence */
    ESP_LOGI(TAG, "[PHASE A] Power-on sequence starting...");
    
    /* SD card power ON */
    gpio_set_level(SD_POWER_EN_PIN, 1);
    ESP_LOGI(TAG, "[POWER]   GPIO%d (SD_POWER_EN) → HIGH (SD powered)", SD_POWER_EN_PIN);
    vTaskDelay(pdMS_TO_TICKS(C6_BOOT_DELAY_MS));
    
    /* Release C6 from reset (boots immediately with GPIO 18 HIGH) */
    gpio_set_level(C6_CHIP_PU_PIN, 1);
    ESP_LOGI(TAG, "[POWER]   GPIO%d (C6_CHIP_PU) → HIGH (C6 released)", C6_CHIP_PU_PIN);
    vTaskDelay(pdMS_TO_TICKS(C6_BOOT_DELAY_MS));
    
    ESP_LOGI(TAG, "[POWER] Rails stabilized, releasing C6 reset.");
    ESP_LOGI(TAG, "[PHASE A] ✓ POWER_READY");
    
    return ESP_OK;
}

/**
 * @brief Initialize Board Support Package
 * 
 * Entry point for BSP initialization. Currently implements Phase A only.
 */
esp_err_t bsp_board_init(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Guition BSP v1.1.0-dev");
    ESP_LOGI(TAG, "  Phase A: Power Manager");
    ESP_LOGI(TAG, "========================================");
    
    esp_err_t ret = bsp_phase_a_power_manager();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase A failed: %s", esp_err_to_name(ret));
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
    ESP_LOGI(TAG, "BSP deinitialized");
}
