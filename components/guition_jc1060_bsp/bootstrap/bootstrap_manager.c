/*
 * Bootstrap Manager Implementation - Simplified
 * 
 * Phase A (Power) is now handled by BSP.
 * Bootstrap manager handles:
 * - Phase C: WiFi initialization
 * - Phase B: SD card mounting (OPTIONAL - respects CONFIG_BSP_ENABLE_SDCARD)
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
#include "sdkconfig.h"  // For CONFIG_BSP_ENABLE_SDCARD

// External functions
#ifdef CONFIG_BSP_ENABLE_SDCARD
extern esp_err_t sd_card_mount_safe(sdmmc_card_t **card);
#endif

#ifdef CONFIG_BSP_ENABLE_WIFI
extern void init_wifi(void);  // From esp_hosted_wifi.c
#endif

static const char *TAG = "BOOTSTRAP";

#ifdef CONFIG_BSP_ENABLE_WIFI
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
#endif // CONFIG_BSP_ENABLE_WIFI

#ifdef CONFIG_BSP_ENABLE_SDCARD
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
#endif // CONFIG_BSP_ENABLE_SDCARD

/**
 * Initialize bootstrap manager
 * 
 * Note: Phase A (Power) is now handled by BSP (bsp_board_init).
 * Bootstrap manager only handles Phase C (WiFi) and Phase B (SD).
 * 
 * Both phases are optional and controlled by Kconfig:
 * - CONFIG_BSP_ENABLE_WIFI enables Phase C
 * - CONFIG_BSP_ENABLE_SDCARD enables Phase B
 */
esp_err_t bootstrap_manager_init(bootstrap_manager_t *manager)
{
    if (!manager) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Bootstrap Manager v1.2.0");
    
#if defined(CONFIG_BSP_ENABLE_WIFI) && defined(CONFIG_BSP_ENABLE_SDCARD)
    ESP_LOGI(TAG, "  Sequence: WiFi → SD");
#elif defined(CONFIG_BSP_ENABLE_WIFI)
    ESP_LOGI(TAG, "  Sequence: WiFi Only (SD disabled)");
#elif defined(CONFIG_BSP_ENABLE_SDCARD)
    ESP_LOGI(TAG, "  Sequence: SD Only (WiFi disabled)");
#else
    ESP_LOGW(TAG, "  No peripherals enabled!");
#endif
    
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
    
#ifdef CONFIG_BSP_ENABLE_WIFI
    // Phase C: WiFi init (initializes SDMMC controller)
    ret = bootstrap_wifi_sequence();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase C failed: %s", esp_err_to_name(ret));
        return ret;
    }
#else
    ESP_LOGI(TAG, "[Phase C] WiFi disabled via Kconfig");
#endif
    
#ifdef CONFIG_BSP_ENABLE_SDCARD
    // Phase B: SD mount (uses dummy init)
    ret = bootstrap_sd_sequence(&manager->sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase B failed: %s", esp_err_to_name(ret));
        return ret;
    }
#else
    ESP_LOGI(TAG, "[Phase B] SD Card disabled via Kconfig");
    ESP_LOGI(TAG, "[Phase B] ✓ Skipped (SD Card disabled)");
    manager->sd_card = NULL;
#endif
    
    uint32_t elapsed_ms = (esp_timer_get_time() / 1000) - manager->boot_timestamp_ms;
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Bootstrap COMPLETE (%u ms)", elapsed_ms);
    
#ifdef CONFIG_BSP_ENABLE_WIFI
    ESP_LOGI(TAG, "  Phase C: WiFi ✓ (SDMMC initialized)");
#else
    ESP_LOGI(TAG, "  Phase C: WiFi ✗ (disabled)");
#endif
    
#ifdef CONFIG_BSP_ENABLE_SDCARD
    ESP_LOGI(TAG, "  Phase B: SD card ✓ (dummy init)");
#else
    ESP_LOGI(TAG, "  Phase B: SD card ✗ (disabled)");
#endif
    
#if defined(CONFIG_BSP_ENABLE_WIFI) && !defined(CONFIG_BSP_ENABLE_SDCARD)
    ESP_LOGI(TAG, "  Mode: WiFi-Only (Recommended)");
#endif
    
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
    
#ifdef CONFIG_BSP_ENABLE_SDCARD
    return manager->sd_card;
#else
    ESP_LOGW(TAG, "SD Card disabled in Kconfig");
    return NULL;
#endif
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
