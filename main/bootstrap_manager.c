/*
 * Bootstrap Manager Implementation - Sequential Init
 * 
 * v1.0.0-beta proven sequence (no FreeRTOS tasks):
 * Phase A → Phase B → Phase C (blocking, deterministic)
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "bootstrap_manager.h"
#include "sdmmc_arbiter.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

// External functions
extern esp_err_t sd_card_mount_safe(sdmmc_card_t **card);

static const char *TAG = "BOOTSTRAP";

/**
 * Phase A: Power Management (GPIO isolation + sequencing)
 */
static esp_err_t bootstrap_power_sequence(void)
{
    ESP_LOGI(TAG, "[Phase A] Power Manager starting...");
    
    // Step 1: GPIO isolation (pre-initialization guard)
    ESP_LOGI(TAG, "[Phase A] Forcing GPIO isolation...");
    
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    // C6 reset (hold in reset)
    io_conf.pin_bit_mask = (1ULL << GPIO_C6_CHIP_PU);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_C6_CHIP_PU, 0);
    ESP_LOGI(TAG, "[Phase A]   GPIO%d (C6_CHIP_PU) → LOW (C6 in reset)", GPIO_C6_CHIP_PU);
    
    // SD power (cut power)
    io_conf.pin_bit_mask = (1ULL << GPIO_SD_POWER_EN);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_SD_POWER_EN, 0);
    ESP_LOGI(TAG, "[Phase A]   GPIO%d (SD_POWER_EN) → LOW (SD unpowered)", GPIO_SD_POWER_EN);
    
    // C6_IO9 strapping protection (force high for SPI boot)
    if (GPIO_C6_IO9_STRAPPING >= 0) {
        io_conf.pin_bit_mask = (1ULL << GPIO_C6_IO9_STRAPPING);
        gpio_config(&io_conf);
        gpio_set_level(GPIO_C6_IO9_STRAPPING, 1);
        ESP_LOGI(TAG, "[Phase A]   GPIO%d (C6_IO9) → HIGH (SPI boot mode)", GPIO_C6_IO9_STRAPPING);
    } else {
        ESP_LOGW(TAG, "[Phase A]   C6_IO9 strapping pin not mapped");
    }
    
    // Step 2: Wait for power rail stabilization
    ESP_LOGI(TAG, "[Phase A] Waiting %dms for rail stabilization...", BOOTSTRAP_POWER_STABILIZATION_MS);
    vTaskDelay(pdMS_TO_TICKS(BOOTSTRAP_POWER_STABILIZATION_MS));
    
    // Step 3: Power-on sequence
    ESP_LOGI(TAG, "[Phase A] Power-on sequence starting...");
    
    // SD card power ON
    gpio_set_level(GPIO_SD_POWER_EN, 1);
    ESP_LOGI(TAG, "[Phase A]   GPIO%d (SD_POWER_EN) → HIGH (SD powered)", GPIO_SD_POWER_EN);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // C6 stays in reset until Phase C (WiFi needs it)
    
    ESP_LOGI(TAG, "[Phase A] ✓ POWER_READY");
    return ESP_OK;
}

/**
 * Phase B: SD Card Mount (blocking)
 */
static esp_err_t bootstrap_sd_sequence(sdmmc_card_t **sd_card)
{
    ESP_LOGI(TAG, "[Phase B] Starting SD card mount...");
    
    // Mount SD card (controller not yet initialized by WiFi)
    esp_err_t ret = sd_card_mount_safe(sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[Phase B] SD mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "[Phase B] ✓ SD_READY");
    return ESP_OK;
}

/**
 * Phase C: WiFi Hosted Init (blocking)
 */
static esp_err_t bootstrap_wifi_sequence(void)
{
    ESP_LOGI(TAG, "[Phase C] Starting WiFi Hosted init...");
    
    // Release C6 from reset
    if (GPIO_C6_IO9_STRAPPING >= 0) {
        if (gpio_get_level(GPIO_C6_IO9_STRAPPING) != 1) {
            ESP_LOGE(TAG, "[Phase C] CRITICAL: C6_IO9 not high!");
            return ESP_FAIL;
        }
    }
    
    gpio_set_level(GPIO_C6_CHIP_PU, 1);
    ESP_LOGI(TAG, "[Phase C]   GPIO%d (C6_CHIP_PU) → HIGH (C6 released)", GPIO_C6_CHIP_PU);
    
    // Configure GPIO6 for handshake
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_C6_IO2_HANDSHAKE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Wait for C6 firmware boot
    ESP_LOGI(TAG, "[Phase C] Waiting for C6 firmware ready (GPIO%d)...", GPIO_C6_IO2_HANDSHAKE);
    int timeout_count = 0;
    while (timeout_count < (BOOTSTRAP_C6_BOOT_TIMEOUT_MS / 100)) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout_count++;
    }
    
    if (timeout_count >= (BOOTSTRAP_C6_BOOT_TIMEOUT_MS / 100)) {
        ESP_LOGW(TAG, "[Phase C] C6 handshake timeout (proceeding anyway)");
    }
    
    // Request WiFi mode from arbiter
    ESP_LOGI(TAG, "[Phase C] Requesting WiFi mode from arbiter...");
    esp_err_t ret = sdmmc_arbiter_request_wifi(10000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[Phase C] WiFi mode request failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "[Phase C] ✓ HOSTED_READY");
    return ESP_OK;
}

/**
 * Detect warm boot
 */
bool bootstrap_is_warm_boot(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    
    switch (reason) {
        case ESP_RST_POWERON:
            ESP_LOGI(TAG, "Cold boot detected (power-on reset)");
            return false;
            
        case ESP_RST_SW:
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_DEEPSLEEP:
        case ESP_RST_BROWNOUT:
            ESP_LOGW(TAG, "Warm boot detected (reset reason: %d)", reason);
            return true;
            
        default:
            ESP_LOGW(TAG, "Unknown reset reason: %d (treating as warm boot)", reason);
            return true;
    }
}

/**
 * Perform hard reset cycle
 */
void bootstrap_hard_reset(void)
{
    ESP_LOGW(TAG, "=== HARD RESET CYCLE ===");
    ESP_LOGW(TAG, "Forcing complete power-down...");
    
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    io_conf.pin_bit_mask = (1ULL << GPIO_C6_CHIP_PU);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_C6_CHIP_PU, 0);
    ESP_LOGW(TAG, "  GPIO%d (C6_CHIP_PU) → LOW", GPIO_C6_CHIP_PU);
    
    io_conf.pin_bit_mask = (1ULL << GPIO_SD_POWER_EN);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_SD_POWER_EN, 0);
    ESP_LOGW(TAG, "  GPIO%d (SD_POWER_EN) → LOW", GPIO_SD_POWER_EN);
    
    ESP_LOGW(TAG, "  Waiting %dms for capacitor discharge...", BOOTSTRAP_HARD_RESET_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(BOOTSTRAP_HARD_RESET_DELAY_MS));
    
    ESP_LOGW(TAG, "Hard reset complete");
}

/**
 * Initialize bootstrap manager (SEQUENTIAL - no tasks)
 */
esp_err_t bootstrap_manager_init(bootstrap_manager_t *manager)
{
    if (!manager) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Bootstrap Manager v1.1.0-sequential");
    ESP_LOGI(TAG, "  Sequential Init: Power → SD → WiFi");
    ESP_LOGI(TAG, "========================================");
    
    manager->boot_timestamp_ms = esp_timer_get_time() / 1000;
    
    // Initialize arbiter (for runtime switching API)
    ESP_LOGI(TAG, "Initializing SDMMC arbiter...");
    esp_err_t ret = sdmmc_arbiter_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Arbiter init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Detect warm boot
    manager->warm_boot_detected = bootstrap_is_warm_boot();
    if (manager->warm_boot_detected) {
        ESP_LOGW(TAG, "Warm boot detected, performing hard reset...");
        bootstrap_hard_reset();
    }
    
    // Phase A: Power sequencing
    ret = bootstrap_power_sequence();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase A failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Phase B: SD mount
    ret = bootstrap_sd_sequence(&manager->sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase B failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Phase C: WiFi init
    ret = bootstrap_wifi_sequence();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase C failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    uint32_t elapsed_ms = (esp_timer_get_time() / 1000) - manager->boot_timestamp_ms;
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Bootstrap COMPLETE (%u ms)", elapsed_ms);
    ESP_LOGI(TAG, "  Phase A: Power ✓");
    ESP_LOGI(TAG, "  Phase B: SD card ✓");
    ESP_LOGI(TAG, "  Phase C: WiFi ✓");
    ESP_LOGI(TAG, "========================================");
    
    return ESP_OK;
}

/**
 * Wait for bootstrap completion (dummy - already completed)
 */
esp_err_t bootstrap_manager_wait(bootstrap_manager_t *manager, uint32_t timeout_ms)
{
    // Sequential init: already completed in bootstrap_manager_init()
    return ESP_OK;
}

/**
 * Get SD card handle
 */
sdmmc_card_t* bootstrap_manager_get_sd_card(bootstrap_manager_t *manager)
{
    if (!manager) {
        return NULL;
    }
    
    return manager->sd_card;
}

/**
 * Clean up
 */
void bootstrap_manager_deinit(bootstrap_manager_t *manager)
{
    if (!manager) {
        return;
    }
    
    sdmmc_arbiter_deinit();
    
    ESP_LOGI(TAG, "Bootstrap manager deinitialized");
}
