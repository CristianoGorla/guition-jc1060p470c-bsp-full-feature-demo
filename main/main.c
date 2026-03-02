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
#include "esp_netif.h"
#include "esp_wifi.h"
#include "rtc_test.h"
#include "rtc_ntp_sync.h"
#include "esp_hosted_wifi.h"
#include "bootstrap_manager.h"

#ifdef CONFIG_APP_ENABLE_WIFI_CONNECT
#include "wifi_config.h"
#endif

#ifdef CONFIG_BSP_ENABLE_LVGL
#include "lvgl.h"

static void lvgl_create_test_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x003366), 0);
    
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, 
        "LVGL v9.2.2 Ready!\n"
        "Display: JD9165 (1024x600)\n"
        "Touch: GT911 (I2C 0x14)\n"
        "Memory: PSRAM 32MB @ 200MHz"
    );
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -80);
    
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 300, 100);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF6600), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF9933), LV_STATE_PRESSED);
    
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Touch Test");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_28, 0);
    lv_obj_center(btn_label);
    
    lv_obj_t *version_label = lv_label_create(scr);
    lv_label_set_text_fmt(version_label, "ESP32-P4 | BSP v1.3.0 | LVGL v%d.%d.%d",
                          LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    lv_obj_set_style_text_color(version_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(version_label, &lv_font_montserrat_14, 0);
    lv_obj_align(version_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);
}
#endif

static const char *TAG = "GUITION_MAIN";

void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "   Guition JC1060P470C Init");
    ESP_LOGI(TAG, "   v1.3.0-dev | Build: %s", BUILD_GIT_COMMIT);
    ESP_LOGI(TAG, "========================================\n");

    ret = bsp_board_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BSP init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "✓ Hardware ready\n");

#ifdef CONFIG_BSP_ENABLE_LVGL
    ESP_LOGI(TAG, "=== LVGL Test UI ===");
    lvgl_create_test_ui();
    ESP_LOGI(TAG, "✓ UI displayed\n");
#endif

#ifdef CONFIG_APP_ENABLE_NVS
    ESP_LOGI(TAG, "=== NVS Init ===");
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✓ NVS ready\n");
#endif

    bootstrap_manager_t bootstrap_mgr = {0};
    
#if defined(CONFIG_BSP_ENABLE_SDCARD) || defined(CONFIG_BSP_ENABLE_WIFI)
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
#endif

    ESP_LOGI(TAG, "\n=== System Ready ===");
    ESP_LOGI(TAG, "Entering main loop...\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Uptime: %lu s", xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);
    }
}
