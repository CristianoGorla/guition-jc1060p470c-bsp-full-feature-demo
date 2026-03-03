/**
 * @file sd_card_manager.c
 * @brief SD Card manager implementation for bootstrap system
 * 
 * CRITICAL FIX: WiFi initializes SDMMC for Slot 1, but SD needs Slot 0.
 * Must deinit/reinit controller before accessing different slot.
 * 
 * RACE CONDITION FIX (0x108): Suspend ESP-Hosted transport BEFORE
 * sdmmc_host_deinit() to prevent H_SDIO_DRV from accessing bus during
 * controller reinitialization. Transport pause now performs full WiFi
 * deinit to guarantee ALL tasks are terminated.
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "sd_card_manager.h"
#include "esp_hosted_wifi.h"  // For transport pause/resume during slot switch
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "SD_MANAGER";

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
    
    ESP_LOGI(TAG, "Enabling pull-ups on SDMMC pins (GPIO39-44)...");
    
    for (int i = 0; i < sizeof(sdmmc_pins) / sizeof(sdmmc_pins[0]); i++) {
        gpio_pullup_en(sdmmc_pins[i]);
    }
}

esp_err_t sd_card_mount_safe(sdmmc_card_t **out_card)
{
    if (g_is_mounted) {
        ESP_LOGW(TAG, "SD card already mounted");
        if (out_card) {
            *out_card = g_sd_card;
        }
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "=== SD Card Mount (Phase B - after WiFi) ===");
    
    // Step 1: SD card power (managed by BSP Phase A, already on)
    ESP_LOGI(TAG, "SD Card power already enabled by BSP Phase A (GPIO%d)", CONFIG_BSP_PIN_SD_POWER_EN);
    
    // Step 2: Enable pull-ups BEFORE mount
    sd_card_enable_pullups();
    
    // Step 3: CRITICAL - Suspend ESP-Hosted transport BEFORE SDMMC deinit
    // This fully deinitializes WiFi, killing ALL ESP-Hosted tasks
    // The pause function includes proper delays for task termination
    ESP_LOGI(TAG, "[BUS ARBITRATION] WiFi transport suspended for bus arbitration");
    esp_hosted_pause_transport();
    
    // No extra delay needed - pause function handles task termination wait
    
    // Step 4: Reinitialize controller for Slot 0
    // WiFi initialized SDMMC for Slot 1, we need to switch to Slot 0
    ESP_LOGI(TAG, "Deinitializing SDMMC controller (release Slot 1 from WiFi)...");
    esp_err_t ret = sdmmc_host_deinit();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Host deinit failed: %s", esp_err_to_name(ret));
        // CRITICAL: Resume transport even on failure
        esp_hosted_resume_transport();
        return ret;
    }
    
    vTaskDelay(pdMS_TO_TICKS(200));  // Bus settling time
    
    ESP_LOGI(TAG, "Reinitializing SDMMC controller for Slot 0...");
    ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Host init failed: %s (0x%x)", esp_err_to_name(ret), ret);
        // CRITICAL: Resume transport even on failure
        esp_hosted_resume_transport();
        return ret;
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));  // Controller stabilization
    
    // Step 5: Configure Slot 0 pins
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
    
    // Step 6: Init Slot 0
    ESP_LOGI(TAG, "Initializing SDMMC Slot 0...");
    ret = sdmmc_host_init_slot(SDMMC_HOST_SLOT_0, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Slot 0 init failed: %s (0x%x)", esp_err_to_name(ret), ret);
        // CRITICAL: Resume transport even on failure
        esp_hosted_resume_transport();
        return ret;
    }
    
    // Step 7: Configure mount options
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    // Step 8: Mount filesystem
    ESP_LOGI(TAG, "Mounting FAT filesystem on Slot 0...");
    
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    
    ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &g_sd_card);
    
    if (ret == ESP_OK) {
        g_is_mounted = true;
        if (out_card) {
            *out_card = g_sd_card;
        }
        ESP_LOGI(TAG, "✓ SD card mounted successfully");
        ESP_LOGI(TAG, "   Card: %s", g_sd_card->cid.name);
        ESP_LOGI(TAG, "   Capacity: %llu MB",
                ((uint64_t)g_sd_card->csd.capacity) * g_sd_card->csd.sector_size / (1024 * 1024));
        
        // Step 9: Resume WiFi transport after successful mount
        ESP_LOGI(TAG, "[BUS ARBITRATION] WiFi transport resumed on Slot 1");
        esp_hosted_resume_transport();
        
        ESP_LOGI(TAG, "");  // Blank line for log readability
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Mount failed: %s (0x%x)", esp_err_to_name(ret), ret);
        
        // CRITICAL: Resume transport even on mount failure
        esp_hosted_resume_transport();
        
        ESP_LOGI(TAG, "");
        return ret;
    }
}

esp_err_t sd_card_unmount(void)
{
    if (!g_is_mounted) {
        ESP_LOGW(TAG, "SD card not mounted");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Unmounting SD card...");
    esp_err_t ret = esp_vfs_fat_sdcard_unmount("/sdcard", g_sd_card);
    
    if (ret == ESP_OK) {
        g_is_mounted = false;
        g_sd_card = NULL;
        ESP_LOGI(TAG, "✓ SD card unmounted");
    } else {
        ESP_LOGE(TAG, "Unmount failed: %s (0x%x)", esp_err_to_name(ret), ret);
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
