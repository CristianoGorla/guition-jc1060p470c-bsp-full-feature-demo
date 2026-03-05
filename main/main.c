/*
 * Guition JC1060P470C BSP - Full Feature Demo
 * Copyright (c) 2026 Cristiano Gorla | SPDX-License-Identifier: Unlicense
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
#ifdef CONFIG_BSP_ENABLE_HEARTBEAT
#include "bsp_heartbeat.h"
#endif
#include "esp_netif.h"
#include "esp_wifi.h"
#include "rtc_test.h"
#include "rtc_ntp_sync.h"
#include "esp_hosted_wifi.h"
#include "bootstrap_manager.h"
#include "backlight_test.h"

#ifdef CONFIG_APP_ENABLE_WIFI_CONNECT
#include "wifi_config.h"
#endif

#ifdef CONFIG_BSP_ENABLE_LVGL
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "lvgl_init.h"
#include "lvgl_demo.h"
#endif

static const char *TAG = "GUITION_MAIN";

void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "   Guition JC1060P470C Init");
    ESP_LOGI(TAG, "   v1.3.0-dev | Build: %s", BUILD_GIT_COMMIT);
    ESP_LOGI(TAG, "========================================\n");

    /* Step 1: BSP - Hardware Only (Display/Touch/Audio/RTC drivers) */
    ret = bsp_board_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BSP init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "[OK] Hardware ready\n");

    /* Step 2: NVS Init */
#ifdef CONFIG_APP_ENABLE_NVS
    ESP_LOGI(TAG, "=== NVS Init ===");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "[OK] NVS ready\n");
#endif

    /* Step 3: Bootstrap - WiFi + SD (PSRAM allocations happen here) */    
#if defined(CONFIG_BSP_ENABLE_SDCARD) || defined(CONFIG_BSP_ENABLE_WIFI)
        bootstrap_manager_t bootstrap_mgr = {0};
    ret = bootstrap_manager_init(&bootstrap_mgr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bootstrap init failed");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    
    ret = bootstrap_manager_wait(&bootstrap_mgr, 30000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bootstrap timeout/failed");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }
    
    ESP_LOGI(TAG, "\n=== Bootstrap Complete ===");
#endif

    /* Step 4: LVGL Init - AFTER Bootstrap (safe PSRAM allocation) */
#ifdef CONFIG_BSP_ENABLE_LVGL
    ESP_LOGI(TAG, "\n=== LVGL Init (Post-Bootstrap) ===");
    ret = lvgl_port_init_custom();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "[OK] LVGL initialized\n");
    }
    
    ESP_LOGI(TAG, "Waiting 2s for LVGL task to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "Stabilization complete");
#endif

    /* Step 5: LVGL UI Creation */
#ifdef CONFIG_BSP_ENABLE_LVGL
    ESP_LOGI(TAG, "=== LVGL UI ===");
    
    /* FIX: Use simple demo instead of widgets to test rendering with swap_xy=false */
    ESP_LOGI(TAG, "Starting LVGL simple demo (testing display/touch)...");
    lvgl_demo_simple();
    
    ESP_LOGI(TAG, "[OK] UI displayed\n");
#endif

    /* ========== HARDWARE TESTS (ALL ENABLED) ========== */
    
    /* Test 1: RTC */
#ifdef CONFIG_BSP_ENABLE_RTC
    ESP_LOGI(TAG, "\n=== RTC Test ===");
    i2c_master_bus_handle_t i2c_bus = bsp_i2c_get_bus_handle();
    if (i2c_bus) {
        rtc_test_read_only(i2c_bus);
    } else {
        ESP_LOGW(TAG, "I2C bus not available");
    }
    ESP_LOGI(TAG, "[OK] RTC test complete\n");
#endif

    /* Test 2: SD Card Info */
#ifdef CONFIG_BSP_ENABLE_SDCARD
    ESP_LOGI(TAG, "\n=== SD Card Test ===");
    sdmmc_card_t *card = bootstrap_mgr.sd_card;
    if (card) {
        uint64_t capacity_mb = ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024);
        ESP_LOGI(TAG, "SD Card Info:");
        ESP_LOGI(TAG, "   Name: %s", card->cid.name);
        ESP_LOGI(TAG, "   Size: %llu MB", capacity_mb);
        ESP_LOGI(TAG, "   Speed: %s", (card->csd.tr_speed > 25000000) ? "High Speed" : "Default Speed");
        ESP_LOGI(TAG, "   Sector size: %d bytes", card->csd.sector_size);
        ESP_LOGI(TAG, "   Capacity: %d sectors", card->csd.capacity);
    } else {
        ESP_LOGW(TAG, "SD card not available");
    }
    ESP_LOGI(TAG, "[OK] SD card test complete\n");
#endif

    /* Test 3: WiFi Scan */
#ifdef CONFIG_BSP_ENABLE_WIFI
    ESP_LOGI(TAG, "\n=== WiFi Scan Test ===");
    do_wifi_scan_and_check(NULL);
    ESP_LOGI(TAG, "[OK] WiFi scan complete\n");
#endif

    /* Test 4: WiFi Connect */
#ifdef CONFIG_APP_ENABLE_WIFI_CONNECT
    ESP_LOGI(TAG, "\n=== WiFi Connect Test ===");
    ESP_LOGI(TAG, "Connecting to: %s", WIFI_SSID);
    
    wifi_connect(WIFI_SSID, WIFI_PASSWORD);
    ESP_LOGI(TAG, "Waiting for IP address...");
    wait_for_ip();
    
    // Check if we got IP
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        if (ip_info.ip.addr != 0) {
            ESP_LOGI(TAG, "[OK] WiFi connected!");
            ESP_LOGI(TAG, "   IP: " IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(TAG, "   GW: " IPSTR, IP2STR(&ip_info.gw));
            ESP_LOGI(TAG, "   Netmask: " IPSTR "\n", IP2STR(&ip_info.netmask));
        } else {
            ESP_LOGW(TAG, "WiFi connection timeout\n");
        }
    } else {
        ESP_LOGW(TAG, "Could not get IP info\n");
    }
#endif

    /* Test 5: Audio (ready but commented) */
#if 0 && defined(CONFIG_BSP_ENABLE_AUDIO)
    ESP_LOGI(TAG, "\n=== Audio Test ===");
    // TODO: Add audio playback test
    ESP_LOGI(TAG, "[OK] Audio test complete\n");
#endif

    /* ========== END TESTS ========== */

    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "   ALL HARDWARE TESTS COMPLETE");
    ESP_LOGI(TAG, "========================================\n");
    
    ESP_LOGI(TAG, "=== System Ready ===\n");

#ifdef CONFIG_BSP_ENABLE_HEARTBEAT
    /* Start BSP heartbeat monitoring (if enabled in menuconfig) */
    ret = bsp_heartbeat_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start heartbeat: %s", esp_err_to_name(ret));
    }
#endif

    /* Main loop now just sleeps - heartbeat runs in background task */
    ESP_LOGI(TAG, "Entering idle loop...\n");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
