/*
 * Backlight Test - Verify Display Hardware
 * 
 * This test creates a white screen and sweeps the backlight
 * from max to min, back to max, then to min again.
 * 
 * If you see brightness changing = display HW works!
 * If screen stays white/black = LVGL rendering issue
 */

#include "backlight_test.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "../components/guition_jc1060_bsp/drivers/jd9165_bsp.h"

static const char *TAG = "BACKLIGHT_TEST";

void backlight_test_run(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Backlight Sweep Test");
    ESP_LOGI(TAG, "  White Screen + PWM Fade");
    ESP_LOGI(TAG, "========================================");
    
    // Lock LVGL to create white screen
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "Failed to lock LVGL!");
        return;
    }
    
    // Create full-screen white object
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    
    // Add center label
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "BACKLIGHT TEST\nWatch brightness change");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    
    // Load screen
    lv_screen_load(screen);
    
    lvgl_port_unlock();
    
    ESP_LOGI(TAG, "White screen loaded, starting sweep...");
    
    // Give LVGL time to render
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // SWEEP 1: 100% → 0% (fade out)
    ESP_LOGI(TAG, "[1/4] Sweep: 100%% → 0%% (fade out)");
    for (int duty = 100; duty >= 0; duty -= 2) {
        bsp_display_set_brightness(duty);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    ESP_LOGI(TAG, "[2/4] Hold at 0%% (1 sec)");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // SWEEP 2: 0% → 100% (fade in)
    ESP_LOGI(TAG, "[3/4] Sweep: 0%% → 100%% (fade in)");
    for (int duty = 0; duty <= 100; duty += 2) {
        bsp_display_set_brightness(duty);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    ESP_LOGI(TAG, "[4/4] Hold at 100%% (1 sec)");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // SWEEP 3: 100% → 0% (final fade out)
    ESP_LOGI(TAG, "[5/4] Sweep: 100%% → 0%% (final)");
    for (int duty = 100; duty >= 0; duty -= 2) {
        bsp_display_set_brightness(duty);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Restore 100% brightness
    ESP_LOGI(TAG, "Restoring brightness to 100%%");
    bsp_display_set_brightness(100);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ✓ Backlight test complete");
    ESP_LOGI(TAG, "  If you SAW brightness changes:");
    ESP_LOGI(TAG, "    → Display HW works! Problem is LVGL");
    ESP_LOGI(TAG, "  If screen stayed WHITE/BLACK:");
    ESP_LOGI(TAG, "    → Check MIPI-DSI init or panel power");
    ESP_LOGI(TAG, "========================================");
}
