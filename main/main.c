/*
 * Guition JC1060P470C BSP - Full Feature Demo
 * 
 * Main application entry point for ESP32-P4 board initialization
 * and feature demonstration.
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "build_info.h"
#include "bsp_board.h"

// Network includes
#include "esp_netif.h"
#include "esp_wifi.h"

// Bootstrap and utilities
#include "rtc_test.h"
#include "rtc_ntp_sync.h"
#include "esp_hosted_wifi.h"
#include "bootstrap_manager.h"

#ifdef CONFIG_BSP_ENABLE_DISPLAY
#include "jd9165_bsp.h"
#endif

#ifdef CONFIG_APP_ENABLE_DISPLAY_TEST
#include "display_hw_test.h"
#endif

#ifdef CONFIG_APP_ENABLE_WIFI_CONNECT
#include "wifi_config.h"
#endif

static const char *TAG = "GUITION_MAIN";

/**
 * @brief Main application entry point
 * 
 * This function demonstrates the simplified initialization flow after
 * Step 5 refactoring. All hardware and bootstrap code is now in the BSP.
 * 
 * Application Flow:
 * 1. BSP initialization (Phase A: Power, Phase D: Drivers)
 * 2. NVS initialization (for WiFi/BT)
 * 3. Bootstrap manager (Phase C: WiFi, Phase B: SD Card)
 * 4. Application-level features and tests
 */
void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "\n");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   Guition JC1060P470C Initialization");
    ESP_LOGI(TAG, "   v1.3.0-dev (Step 5 Complete)");
    ESP_LOGI(TAG, "   Build: %s", BUILD_GIT_COMMIT);
    ESP_LOGI(TAG, "   Date: %s", BUILD_TIMESTAMP);
    ESP_LOGI(TAG, "========================================\n");

    // ========================================
    // BSP Initialization (Phase A + Phase D)
    // ========================================
    // This single call handles:
    // - Phase A: Power Manager (SD card power sequencing)
    // - Phase D: Peripheral Drivers (display, touch, audio, RTC)
    //   (conditional on Kconfig settings)
    ret = bsp_board_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BSP initialization failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Cannot continue - hardware not ready");
        return;
    }
    
    ESP_LOGI(TAG, "✓ Hardware initialization complete\n");

    // ========================================
    // NVS Initialization
    // ========================================
#ifdef CONFIG_APP_ENABLE_NVS
    ESP_LOGI(TAG, "=== NVS Initialization ===");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✓ NVS initialized\n");
#else
    ESP_LOGI(TAG, "NVS disabled by Kconfig\n");
#endif

    // ========================================
    // Bootstrap Manager (Phase C + Phase B)
    // ========================================
    // Handles:
    // - Phase C: WiFi initialization (ESP-Hosted)
    // - Phase B: SD Card mounting
    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "   Starting Bootstrap Manager");
    ESP_LOGI(TAG, "   Phase C: WiFi + Phase B: SD Card");
    ESP_LOGI(TAG, "========================================\n");
    
    bootstrap_manager_t bootstrap_mgr = {0};
    
#if defined(CONFIG_BSP_ENABLE_SDCARD) || defined(CONFIG_BSP_ENABLE_WIFI)
    ret = bootstrap_manager_init(&bootstrap_mgr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bootstrap init failed: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "Retrying in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    
    ret = bootstrap_manager_wait(&bootstrap_mgr, 30000);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "\n========================================");
        ESP_LOGI(TAG, "   Bootstrap Complete - System Ready");
        ESP_LOGI(TAG, "========================================\n");
        
        // Display SD card info if available
        sdmmc_card_t *card = bootstrap_manager_get_sd_card(&bootstrap_mgr);
        if (card) {
            ESP_LOGI(TAG, "SD Card: %s", card->cid.name);
            ESP_LOGI(TAG, "Capacity: %llu MB\n",
                    ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024));
        }
        
#ifdef CONFIG_APP_ENABLE_WIFI_CONNECT
        // WiFi connection test
        ESP_LOGI(TAG, "=== WiFi Connection Test ===");
        ESP_LOGI(TAG, "Connecting to: %s", WIFI_SSID);
        
        wifi_connect(WIFI_SSID, WIFI_PASSWORD);
        ESP_LOGI(TAG, "Waiting for IP address (15s timeout)...");
        
        wait_for_ip();
        
        if (check_if_already_has_ip()) {
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_ip_info_t ip;
            
            if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
                ESP_LOGI(TAG, "✓ WiFi connected!");
                ESP_LOGI(TAG, "   IP: " IPSTR, IP2STR(&ip.ip));
                ESP_LOGI(TAG, "   Netmask: " IPSTR, IP2STR(&ip.netmask));
                ESP_LOGI(TAG, "   Gateway: " IPSTR, IP2STR(&ip.gw));
                
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    ESP_LOGI(TAG, "   RSSI: %d dBm\n", ap_info.rssi);
                }
                
#if defined(CONFIG_BSP_ENABLE_RTC) && defined(CONFIG_APP_ENABLE_RTC_NTP_SYNC)
                ESP_LOGI(TAG, "=== RTC NTP Sync Test ===");
                rtc_ntp_sync_test();
#endif
            }
        } else {
            ESP_LOGW(TAG, "WiFi connection timeout\n");
        }
#elif defined(CONFIG_BSP_ENABLE_WIFI)
        // Simple WiFi scan test
        ESP_LOGI(TAG, "=== WiFi Scan Test ===");
        if (do_wifi_scan_and_check(NULL)) {
            ESP_LOGI(TAG, "✓ WiFi scan successful\n");
        } else {
            ESP_LOGW(TAG, "No networks found\n");
        }
#endif
        
    } else if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "Bootstrap timeout!");
        ESP_LOGE(TAG, "WiFi or SD card initialization took too long");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Bootstrap failed: %s", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }
#else
    ESP_LOGI(TAG, "Bootstrap manager disabled (no WiFi/SD card enabled)\n");
#endif

    // ========================================
    // Application Features
    // ========================================
    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "   Application Ready");
    ESP_LOGI(TAG, "========================================\n");
    
    ESP_LOGI(TAG, "System fully initialized:");
#ifdef CONFIG_BSP_ENABLE_DISPLAY
    ESP_LOGI(TAG, "  ✓ Display: JD9165 (1024x600)");
#endif
#ifdef CONFIG_BSP_ENABLE_TOUCH
    ESP_LOGI(TAG, "  ✓ Touch: GT911");
#endif
#ifdef CONFIG_BSP_ENABLE_AUDIO
    ESP_LOGI(TAG, "  ✓ Audio: ES8311 + NS4150");
#endif
#ifdef CONFIG_BSP_ENABLE_RTC
    ESP_LOGI(TAG, "  ✓ RTC: RX8025T");
#endif
#ifdef CONFIG_BSP_ENABLE_WIFI
    ESP_LOGI(TAG, "  ✓ WiFi: ESP-Hosted");
#endif
#ifdef CONFIG_BSP_ENABLE_SDCARD
    ESP_LOGI(TAG, "  ✓ SD Card: SDMMC");
#endif
    ESP_LOGI(TAG, "");
    
    // ========================================
    // Hardware Tests
    // ========================================
    
#ifdef CONFIG_APP_ENABLE_DISPLAY_TEST
    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "   Display Hardware Test");
    ESP_LOGI(TAG, "========================================\n");
    
    esp_lcd_panel_handle_t panel_handle = bsp_display_get_panel_handle();
    if (panel_handle) {
        // Test 1: Hardware pattern (faster)
        ESP_LOGI(TAG, "Test 1: Hardware color bar pattern");
        ret = display_hw_test_pattern(panel_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✓ Hardware pattern test passed");
            vTaskDelay(pdMS_TO_TICKS(3000)); // Show for 3 seconds
        } else {
            ESP_LOGW(TAG, "Hardware pattern test failed: %s", esp_err_to_name(ret));
        }
        
        // Test 2: Software rendering
        ESP_LOGI(TAG, "\nTest 2: Software-rendered color bars");
        ret = display_hw_test_color_bar(panel_handle, CONFIG_BSP_DISPLAY_WIDTH, CONFIG_BSP_DISPLAY_HEIGHT);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✓ Software rendering test passed\n");
        } else {
            ESP_LOGW(TAG, "Software rendering test failed: %s\n", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "Display panel handle not available\n");
    }
#endif
    
    ESP_LOGI(TAG, "Entering main loop...\n");
    
    // Main application loop
    while (1)
    {
        // TODO: Add periodic application tasks here
        // For now, just idle
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        // Periodic status log
        ESP_LOGI(TAG, "System running... (uptime: %lu seconds)", 
                 xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
    }
}
