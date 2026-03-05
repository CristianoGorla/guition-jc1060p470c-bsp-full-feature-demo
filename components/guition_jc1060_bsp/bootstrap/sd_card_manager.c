/**
 * @file sd_card_manager.c
 * @brief SD Card manager with Clean Switch Protocol integration
 * 
 * Implements safe SDMMC controller slot switching using CCCR handshake
 * protocol to prevent race condition 0x108 during WiFi→SD arbitration.
 * 
 * PROTOCOL SEQUENCE:
 * 1. esp_hosted_pause_transport() - Clean handshake with C6 slave
 * 2. sdmmc_host_deinit() - Release Slot 1
 * 3. 50ms settling delay - Allow bus capacitance to discharge
 * 4. sdmmc_host_init() - Reinitialize for Slot 0
 * 5. SD card mount
 * 6. esp_hosted_resume_transport() - Restore WiFi on Slot 1
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "sd_card_manager.h"
#include "esp_hosted_wifi.h"  // For Clean Switch Protocol
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "bsp_log_panel.h"

static const char *TAG = BSP_LOG_TAG;

#define LOG_UNIT "SDCARD"
#define LOGI(fmt, ...) BSP_LOGI_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) BSP_LOGW_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) BSP_LOGE_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)

// Global SD card handle
static sdmmc_card_t *g_sd_card = NULL;
static bool g_is_mounted = false;

// Pin definitions from BSP Kconfig (with fallback defaults)
#ifndef CONFIG_BSP_PIN_CLK
#define CONFIG_BSP_PIN_CLK 43
#endif
#ifndef CONFIG_BSP_PIN_CMD
#define CONFIG_BSP_PIN_CMD 44
#endif
#ifndef CONFIG_BSP_PIN_D0
#define CONFIG_BSP_PIN_D0 39
#endif
#ifndef CONFIG_BSP_PIN_D1
#define CONFIG_BSP_PIN_D1 40
#endif
#ifndef CONFIG_BSP_PIN_D2
#define CONFIG_BSP_PIN_D2 41
#endif
#ifndef CONFIG_BSP_PIN_D3
#define CONFIG_BSP_PIN_D3 42
#endif
#ifndef CONFIG_BSP_PIN_SD_POWER_EN
#define CONFIG_BSP_PIN_SD_POWER_EN 36
#endif

/**
 * @brief Enable pull-ups on SDMMC data lines
 * 
 * Internal pull-ups prevent floating signals that cause 0x107 errors.
 */
static void sd_card_enable_pullups(void)
{
    const int sdmmc_pins[] = {
        CONFIG_BSP_PIN_D0,
        CONFIG_BSP_PIN_D1,
        CONFIG_BSP_PIN_D2,
        CONFIG_BSP_PIN_D3,
        CONFIG_BSP_PIN_CMD,
        CONFIG_BSP_PIN_CLK
    };
    
    LOGI( "Enabling pull-ups on SDMMC pins (GPIO39-44)...");
    
    for (int i = 0; i < sizeof(sdmmc_pins) / sizeof(sdmmc_pins[0]); i++) {
        gpio_pullup_en(sdmmc_pins[i]);
    }
}

esp_err_t sd_card_mount_safe(sdmmc_card_t **out_card)
{
    if (g_is_mounted) {
        LOGW( "SD card already mounted");
        if (out_card) {
            *out_card = g_sd_card;
        }
        return ESP_OK;
    }
    
    LOGI( "=== SD Card Mount (Phase B - after WiFi) ===");
    
    // Step 1: SD card power (managed by BSP Phase A, already on)
    LOGI( "SD Card power already enabled by BSP Phase A (GPIO%d)", CONFIG_BSP_PIN_SD_POWER_EN);
    
    // Step 2: Enable pull-ups BEFORE mount
    sd_card_enable_pullups();
    
    // Step 3: CLEAN SWITCH PROTOCOL - Pause WiFi transport with CCCR handshake
    // This performs:
    //   - WiFi stack stop
    //   - CCCR handshake to silence C6 interrupts
    //   - WiFi driver deinit (kills ESP-Hosted tasks)
    //   - Bus idle verification (DAT0 polling)
    LOGI( "[BUS ARBITRATION] Initiating Clean Switch Protocol...");
    esp_hosted_pause_transport();
    
    // Step 4: Deinitialize SDMMC controller (release Slot 1)
    // WiFi initialized SDMMC for Slot 1, we need to switch to Slot 0
    LOGI( "Deinitializing SDMMC controller (release Slot 1)...");
    esp_err_t ret = sdmmc_host_deinit();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        LOGE( "Host deinit failed: %s", esp_err_to_name(ret));
        // CRITICAL: Resume transport even on failure
        esp_hosted_resume_transport();
        return ret;
    }
    
    // Step 5: SETTLING DELAY - Allow bus capacitance to discharge
    // Hardware requirement: GPIO states need time to stabilize after deinit
    LOGI( "Bus settling delay (50ms)...");
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Step 6: Reinitialize controller for Slot 0
    LOGI( "Reinitializing SDMMC controller for Slot 0...");
    ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        LOGE( "Host init failed: %s (0x%x)", esp_err_to_name(ret), ret);
        // CRITICAL: Resume transport even on failure
        esp_hosted_resume_transport();
        return ret;
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));  // Controller stabilization
    
    // Step 7: Configure Slot 0 pins
    sdmmc_slot_config_t slot_config = {
        .clk = CONFIG_BSP_PIN_CLK,
        .cmd = CONFIG_BSP_PIN_CMD,
        .d0 = CONFIG_BSP_PIN_D0,
        .d1 = CONFIG_BSP_PIN_D1,
        .d2 = CONFIG_BSP_PIN_D2,
        .d3 = CONFIG_BSP_PIN_D3,
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
        .width = 4,
        .flags = 0,
    };
    
    // Step 8: Init Slot 0
    LOGI( "Initializing SDMMC Slot 0...");
    ret = sdmmc_host_init_slot(SDMMC_HOST_SLOT_0, &slot_config);
    if (ret != ESP_OK) {
        LOGE( "Slot 0 init failed: %s (0x%x)", esp_err_to_name(ret), ret);
        // CRITICAL: Resume transport even on failure
        esp_hosted_resume_transport();
        return ret;
    }
    
    // Step 9: Configure mount options
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    // Step 10: Mount filesystem
    LOGI( "Mounting FAT filesystem on Slot 0...");
    
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    
    ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &g_sd_card);
    
    if (ret == ESP_OK) {
        g_is_mounted = true;
        if (out_card) {
            *out_card = g_sd_card;
        }
        LOGI( "[OK] SD card mounted successfully");
        LOGI( "   Card: %s", g_sd_card->cid.name);
        LOGI( "   Capacity: %llu MB",
                ((uint64_t)g_sd_card->csd.capacity) * g_sd_card->csd.sector_size / (1024 * 1024));
        
        // Step 11: Resume WiFi transport (Clean Switch Protocol reverse)
        LOGI( "[BUS ARBITRATION] Resuming WiFi transport on Slot 1...");
        esp_hosted_resume_transport();
        
        LOGI( "");  // Blank line for log readability
        return ESP_OK;
    } else {
        LOGE( "Mount failed: %s (0x%x)", esp_err_to_name(ret), ret);
        
        // CRITICAL: Resume transport even on mount failure
        LOGI( "[BUS ARBITRATION] Resuming WiFi transport (mount failed)...");
        esp_hosted_resume_transport();
        
        LOGI( "");
        return ret;
    }
}

esp_err_t sd_card_unmount(void)
{
    if (!g_is_mounted) {
        LOGW( "SD card not mounted");
        return ESP_OK;
    }
    
    LOGI( "Unmounting SD card...");
    esp_err_t ret = esp_vfs_fat_sdcard_unmount("/sdcard", g_sd_card);
    
    if (ret == ESP_OK) {
        g_is_mounted = false;
        g_sd_card = NULL;
        LOGI( "[OK] SD card unmounted");
    } else {
        LOGE( "Unmount failed: %s (0x%x)", esp_err_to_name(ret), ret);
    }
    
    return ret;
}

esp_err_t sd_card_unmount_safe(void)
{
    return sd_card_unmount();
}

bool sd_card_is_mounted(void)
{
    return g_is_mounted;
}

sdmmc_card_t* sd_card_get_handle(void)
{
    return g_is_mounted ? g_sd_card : NULL;
}
