/*
 * Bootstrap Manager Implementation - Simplified
 * 
 * Phase A (Power) is now handled by BSP.
 * Bootstrap manager handles:
 * - Phase C: WiFi initialization
 * - Phase B: SD card mounting
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "bootstrap_manager.h"
#include "sdmmc_arbiter.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

// External functions
extern esp_err_t sd_card_mount_safe(sdmmc_card_t **card);
extern void init_wifi(void);  // From esp_hosted_wifi.c

static const char *TAG = "BOOTSTRAP";

/**
 * Phase C: WiFi Hosted Init
 * 
 * CRITICAL: This must run BEFORE SD mount!
 * init_wifi() initializes the SDMMC controller for ESP-Hosted transport.
 */
static esp_err_t bootstrap_wifi_sequence(void)
{
    ESP_LOGI(TAG, "[Phase C] Starting WiFi transport...");
    ESP_LOGI(TAG, "[Phase C] init_wifi() will initialize SDMMC controller");
    
    init_wifi();  // This initializes SDMMC controller for ESP-Hosted!
    
    // Wait for C6 firmware boot and SDMMC initialization
    ESP_LOGI(TAG, "[Phase C] Waiting 2s for C6 firmware + SDMMC init...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "[Phase C] ✓ WIFI_READY (SDMMC controller initialized)");
    return ESP_OK;
}

/**
 * Phase B: SD Card Mount
 * 
 * MUST run AFTER Phase C (WiFi)!
 * SD uses dummy init because SDMMC controller is already initialized.
 */
static esp_err_t bootstrap_sd_sequence(sdmmc_card_t **sd_card)
{
    ESP_LOGI(TAG, "[Phase B] Starting SD card mount...");
    ESP_LOGI(TAG, "[Phase B] Using dummy init (controller active from WiFi)");
    
    // Mount SD card (controller already initialized by init_wifi)
    esp_err_t ret = sd_card_mount_safe(sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[Phase B] SD mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "[Phase B] ✓ SD_READY");
    return ESP_OK;
}

/**
 * Initialize bootstrap manager
 * 
 * Note: Phase A (Power) is now handled by BSP (bsp_board_init).
 * Bootstrap manager only handles Phase C (WiFi) and Phase B (SD).
 */
esp_err_t bootstrap_manager_init(bootstrap_manager_t *manager)
{
    if (!manager) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Bootstrap Manager v1.2.0");
    ESP_LOGI(TAG, "  Sequence: WiFi → SD");
    ESP_LOGI(TAG, "  (Phase A handled by BSP)");
    ESP_LOGI(TAG, "========================================");
    
    manager->boot_timestamp_ms = esp_timer_get_time() / 1000;
    
    // Initialize arbiter (for runtime switching API)
    ESP_LOGI(TAG, "Initializing SDMMC arbiter...");
    esp_err_t ret = sdmmc_arbiter_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Arbiter init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Phase C: WiFi init (initializes SDMMC controller)
    ret = bootstrap_wifi_sequence();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase C failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Phase B: SD mount (uses dummy init)
    ret = bootstrap_sd_sequence(&manager->sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase B failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    uint32_t elapsed_ms = (esp_timer_get_time() / 1000) - manager->boot_timestamp_ms;
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Bootstrap COMPLETE (%u ms)", elapsed_ms);
    ESP_LOGI(TAG, "  Phase C: WiFi ✓ (SDMMC initialized)");
    ESP_LOGI(TAG, "  Phase B: SD card ✓ (dummy init)");
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
