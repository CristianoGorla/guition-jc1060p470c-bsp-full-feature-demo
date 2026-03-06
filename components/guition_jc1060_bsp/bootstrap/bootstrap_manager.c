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
#include "bsp_log_panel.h"
#include "esp_timer.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "sdkconfig.h"  // For CONFIG_BSP_ENABLE_SDCARD

#ifdef CONFIG_BSP_ENABLE_CAMERA
#include "drivers/ov02c10_wrapper.h"
#endif

// External functions
#ifdef CONFIG_BSP_ENABLE_SDCARD
extern esp_err_t sd_card_mount_safe(sdmmc_card_t **card);
#endif

#ifdef CONFIG_BSP_ENABLE_WIFI
extern void init_wifi(void);  // From esp_hosted_wifi.c
#endif

static const char *TAG = BSP_LOG_TAG;

#define LOG_UNIT "BOOT"
#define LOGI(fmt, ...) BSP_LOGI_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) BSP_LOGW_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) BSP_LOGE_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)

#ifdef CONFIG_BSP_ENABLE_WIFI
/**
 * Phase C: WiFi Hosted Init
 * 
 * CRITICAL: This must run BEFORE SD mount!
 * init_wifi() initializes the SDMMC controller for ESP-Hosted transport.
 */
static esp_err_t bootstrap_wifi_sequence(void)
{
    LOGI( "[Phase C] Starting WiFi transport...");
    LOGI( "[Phase C] init_wifi() will initialize SDMMC controller");
    
    init_wifi();  // This initializes SDMMC controller for ESP-Hosted!
    
    // Wait for C6 firmware boot and SDMMC initialization
    LOGI( "[Phase C] Waiting 2s for C6 firmware + SDMMC init...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    LOGI( "[Phase C] [OK] WIFI_READY (SDMMC controller initialized)");
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
    LOGI( "[Phase B] Starting SD card mount...");
    LOGI( "[Phase B] Using dummy init (controller active from WiFi)");
    
    // Mount SD card (controller already initialized by init_wifi)
    esp_err_t ret = sd_card_mount_safe(sd_card);
    if (ret != ESP_OK) {
        LOGE( "[Phase B] SD mount failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    LOGI( "[Phase B] [OK] SD_READY");
    return ESP_OK;
}
#endif // CONFIG_BSP_ENABLE_SDCARD

#ifdef CONFIG_BSP_ENABLE_CAMERA
static esp_err_t bootstrap_camera_phase_a(void)
{
    LOGI( "[Phase A] Starting camera HW reset...");
    esp_err_t ret = bsp_camera_power_on();
    if (ret != ESP_OK) {
        LOGW( "[Phase A] Camera reset failed: %s", esp_err_to_name(ret));
        return ESP_OK;
    }

    LOGI( "[Phase A] [OK] Camera XSHUTDN sequence complete");
    return ESP_OK;
}

static esp_err_t bootstrap_camera_phase_d(void)
{
    LOGI( "[Phase D] Starting OV02C10 SCCB probe + ISP registration...");
    esp_err_t ret = bsp_camera_init();
    if (ret != ESP_OK) {
        LOGW( "[Phase D] Camera init failed: %s", esp_err_to_name(ret));
        return ESP_OK;
    }

    LOGI( "[Phase D] [OK] OV02C10 ready");
    return ESP_OK;
}
#endif

/**
 * Initialize bootstrap manager
 * 
 * Note: Phase A (Power) is primarily handled by BSP (bsp_board_init).
 * Bootstrap manager handles Phase C (WiFi) and Phase B (SD), and can
 * run camera hooks for Phase A/D when enabled.
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
    
    LOGI( "========================================");
    LOGI( "  Bootstrap Manager v1.2.0");
    
#if defined(CONFIG_BSP_ENABLE_WIFI) && defined(CONFIG_BSP_ENABLE_SDCARD)
    LOGI( "  Sequence: WiFi -> SD");
#elif defined(CONFIG_BSP_ENABLE_WIFI)
    LOGI( "  Sequence: WiFi Only (SD disabled)");
#elif defined(CONFIG_BSP_ENABLE_SDCARD)
    LOGI( "  Sequence: SD Only (WiFi disabled)");
#else
    LOGW( "  No peripherals enabled!");
#endif
    
    LOGI( "  (Phase A handled by BSP)");
    LOGI( "========================================");
    
    manager->boot_timestamp_ms = esp_timer_get_time() / 1000;
    
    // Initialize arbiter (for runtime switching API)
    LOGI( "Initializing SDMMC arbiter...");
    esp_err_t ret = sdmmc_arbiter_init();
    if (ret != ESP_OK) {
        LOGE( "Arbiter init failed: %s", esp_err_to_name(ret));
        return ret;
    }

#ifdef CONFIG_BSP_ENABLE_CAMERA
    ret = bootstrap_camera_phase_a();
    if (ret != ESP_OK) {
        return ret;
    }
#endif
    
#ifdef CONFIG_BSP_ENABLE_WIFI
    // Phase C: WiFi init (initializes SDMMC controller)
    ret = bootstrap_wifi_sequence();
    if (ret != ESP_OK) {
        LOGE( "Phase C failed: %s", esp_err_to_name(ret));
        return ret;
    }
#else
    LOGI( "[Phase C] WiFi disabled via Kconfig");
#endif
    
#ifdef CONFIG_BSP_ENABLE_SDCARD
    // Phase B: SD mount (uses dummy init)
    ret = bootstrap_sd_sequence(&manager->sd_card);
    if (ret != ESP_OK) {
        LOGE( "Phase B failed: %s", esp_err_to_name(ret));
        return ret;
    }
#else
    LOGI( "[Phase B] SD Card disabled via Kconfig");
    LOGI( "[Phase B] [OK] Skipped (SD Card disabled)");
    manager->sd_card = NULL;
#endif

#ifdef CONFIG_BSP_ENABLE_CAMERA
    ret = bootstrap_camera_phase_d();
    if (ret != ESP_OK) {
        return ret;
    }
#endif
    
    uint32_t elapsed_ms = (esp_timer_get_time() / 1000) - manager->boot_timestamp_ms;
    LOGI( "========================================");
    LOGI( "  Bootstrap COMPLETE (%u ms)", elapsed_ms);
    
#ifdef CONFIG_BSP_ENABLE_WIFI
    LOGI( "  Phase C: WiFi [OK] (SDMMC initialized)");
#else
    LOGI( "  Phase C: WiFi [FAIL] (disabled)");
#endif
    
#ifdef CONFIG_BSP_ENABLE_SDCARD
    LOGI( "  Phase B: SD card [OK] (dummy init)");
#else
    LOGI( "  Phase B: SD card [FAIL] (disabled)");
#endif
    
#if defined(CONFIG_BSP_ENABLE_WIFI) && !defined(CONFIG_BSP_ENABLE_SDCARD)
    LOGI( "  Mode: WiFi-Only (Recommended)");
#endif
    
    LOGI( "========================================");
    
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
    LOGW( "SD Card disabled in Kconfig");
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
    
    LOGI( "Bootstrap manager deinitialized");
}
