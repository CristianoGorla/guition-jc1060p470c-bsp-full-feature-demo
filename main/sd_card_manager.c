/**
 * @file sd_card_manager.c
 * @brief SD Card manager implementation for bootstrap system
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "sd_card_manager.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "feature_flags.h"

static const char *TAG = "SD_MANAGER";

// Global SD card handle
static sdmmc_card_t *g_sd_card = NULL;
static bool g_is_mounted = false;

// Pin definitions (from sdkconfig)
#ifndef CONFIG_EXAMPLE_PIN_CLK
#define CONFIG_EXAMPLE_PIN_CLK 43
#endif
#ifndef CONFIG_EXAMPLE_PIN_CMD
#define CONFIG_EXAMPLE_PIN_CMD 44
#endif
#ifndef CONFIG_EXAMPLE_PIN_D0
#define CONFIG_EXAMPLE_PIN_D0 39
#endif
#ifndef CONFIG_EXAMPLE_PIN_D1
#define CONFIG_EXAMPLE_PIN_D1 40
#endif
#ifndef CONFIG_EXAMPLE_PIN_D2
#define CONFIG_EXAMPLE_PIN_D2 41
#endif
#ifndef CONFIG_EXAMPLE_PIN_D3
#define CONFIG_EXAMPLE_PIN_D3 42
#endif
#ifndef CONFIG_EXAMPLE_PIN_CARD_POWER_RESET
#define CONFIG_EXAMPLE_PIN_CARD_POWER_RESET 36
#endif

/**
 * @brief Enable pull-ups on SDMMC data lines
 * 
 * Internal pull-ups prevent floating signals that cause 0x107 errors.
 */
static void sd_card_enable_pullups(void)
{
    const int sdmmc_pins[] = {
        CONFIG_EXAMPLE_PIN_D0,
        CONFIG_EXAMPLE_PIN_D1,
        CONFIG_EXAMPLE_PIN_D2,
        CONFIG_EXAMPLE_PIN_D3,
        CONFIG_EXAMPLE_PIN_CMD,
        CONFIG_EXAMPLE_PIN_CLK
    };
    
    LOG_SD(TAG, "Enabling pull-ups on SDMMC pins (GPIO39-44)...");
    
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
    
    LOG_SD(TAG, "=== SD Card Mount (Phase B - SD before WiFi) ===");
    
    // Step 1: Enable SD card power (if controllable)
#ifdef CONFIG_EXAMPLE_PIN_CARD_POWER_RESET
    gpio_config_t pwr_io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_EXAMPLE_PIN_CARD_POWER_RESET),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr_io_conf);
    gpio_set_level(CONFIG_EXAMPLE_PIN_CARD_POWER_RESET, 1);  // Power on
    vTaskDelay(pdMS_TO_TICKS(250));
    LOG_SD(TAG, "SD Card power enabled (GPIO%d)", CONFIG_EXAMPLE_PIN_CARD_POWER_RESET);
#endif
    
    // Step 2: Enable pull-ups BEFORE init (prevents 0x107 on bus)
    sd_card_enable_pullups();
    
    // Step 3: Configure Slot 0 pins
    sdmmc_slot_config_t slot_config = {
        .clk = CONFIG_EXAMPLE_PIN_CLK,
        .cmd = CONFIG_EXAMPLE_PIN_CMD,
        .d0 = CONFIG_EXAMPLE_PIN_D0,
        .d1 = CONFIG_EXAMPLE_PIN_D1,
        .d2 = CONFIG_EXAMPLE_PIN_D2,
        .d3 = CONFIG_EXAMPLE_PIN_D3,
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
        .width = 4,
        .flags = 0,
    };
    
    // Step 4: Configure mount options
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,  // Don't auto-format
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    // Step 5: Use STANDARD host config (will init controller)
    // SD mounts BEFORE WiFi, so controller is NOT initialized yet
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    // NO dummy functions - use real sdmmc_host_init()
    
    // Step 6: Mount filesystem (this will init host + slot)
    LOG_SD(TAG, "Mounting FAT filesystem (will initialize SDMMC host)...");
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &g_sd_card);
    
    if (ret == ESP_OK) {
        g_is_mounted = true;
        if (out_card) {
            *out_card = g_sd_card;
        }
        LOG_SD(TAG, "✓ SD card mounted successfully");
        LOG_SD(TAG, "   Card: %s", g_sd_card->cid.name);
        LOG_SD(TAG, "   Capacity: %llu MB\n",
                ((uint64_t)g_sd_card->csd.capacity) * g_sd_card->csd.sector_size / (1024 * 1024));
        LOG_SD(TAG, "   SDMMC controller now initialized (WiFi will reuse it)");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Mount failed: %s (0x%x)\n", esp_err_to_name(ret), ret);
        return ret;
    }
}

esp_err_t sd_card_unmount(void)
{
    if (!g_is_mounted) {
        ESP_LOGW(TAG, "SD card not mounted");
        return ESP_OK;
    }
    
    LOG_SD(TAG, "Unmounting SD card...");
    esp_err_t ret = esp_vfs_fat_sdcard_unmount("/sdcard", g_sd_card);
    
    if (ret == ESP_OK) {
        g_is_mounted = false;
        g_sd_card = NULL;
        LOG_SD(TAG, "✓ SD card unmounted");
    } else {
        ESP_LOGE(TAG, "Unmount failed: %s (0x%x)", esp_err_to_name(ret), ret);
    }
    
    return ret;
}

esp_err_t sd_card_unmount_safe(void)
{
    // Wrapper for arbiter integration (same as sd_card_unmount)
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
