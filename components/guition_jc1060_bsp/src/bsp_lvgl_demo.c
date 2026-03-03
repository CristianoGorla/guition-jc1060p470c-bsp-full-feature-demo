/*
 * SPDX-FileCopyrightText: 2024 Cristiano Gorla
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Guition JC1060P470C - LVGL Demo Runner
 */

#include "sdkconfig.h"

#ifdef CONFIG_BSP_ENABLE_LVGL
#ifdef CONFIG_BSP_LVGL_ENABLE_DEMO

#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "lvgl_demo";

/**
 * @brief Simple LVGL demo - minimal memory usage
 * 
 * Creates a basic test screen with:
 * - Colored background
 * - Text label with board info
 * - Button (if touch enabled)
 */
static void lvgl_demo_simple(void)
{
    ESP_LOGI(TAG, "Running simple LVGL demo");

    /* Get active screen (LVGL already initialized by bsp_board_init) */
    lv_obj_t *scr = lv_scr_act();
    if (scr == NULL) {
        ESP_LOGE(TAG, "No active screen - LVGL not initialized?");
        return;
    }
    
    /* Set background color */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x003a57), LV_PART_MAIN);
    
    /* Create title label */
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Guition JC1060P470C\nLVGL v9.2.2 Demo\n\n1024x600 MIPI-DSI\nESP32-P4 @ 400MHz");
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -50);

#ifdef CONFIG_BSP_LVGL_TOUCH_ENABLE
    /* Create test button */
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 80);
    
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Touch Me!");
    lv_obj_center(btn_label);
#else
    /* Info label if no touch */
    lv_obj_t *info = lv_label_create(scr);
    lv_label_set_text(info, "(Touch disabled)");
    lv_obj_set_style_text_color(info, lv_color_hex(0xaaaaaa), LV_PART_MAIN);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 80);
#endif

    ESP_LOGI(TAG, "Simple demo started");
}

/**
 * @brief Run LVGL demo based on Kconfig selection
 * 
 * Called by main.c after LVGL initialization.
 * Launches the demo type selected in menuconfig.
 */
void lvgl_demo_run_from_config(void)
{
    ESP_LOGI(TAG, "Starting LVGL demo...");

#ifdef CONFIG_BSP_LVGL_DEMO_SIMPLE
    lvgl_demo_simple();

#elif defined(CONFIG_BSP_LVGL_DEMO_WIDGETS)
    ESP_LOGI(TAG, "Running LVGL widgets demo");
    /* TODO: Add lv_demo_widgets() from lvgl examples */
    ESP_LOGW(TAG, "Widgets demo not yet implemented, falling back to simple demo");
    lvgl_demo_simple();

#elif defined(CONFIG_BSP_LVGL_DEMO_BENCHMARK)
    ESP_LOGI(TAG, "Running LVGL benchmark demo");
    /* TODO: Add lv_demo_benchmark() from lvgl examples */
    ESP_LOGW(TAG, "Benchmark demo not yet implemented, falling back to simple demo");
    lvgl_demo_simple();

#elif defined(CONFIG_BSP_LVGL_DEMO_STRESS)
    ESP_LOGI(TAG, "Running LVGL stress test demo");
    /* TODO: Add lv_demo_stress() from lvgl examples */
    ESP_LOGW(TAG, "Stress test demo not yet implemented, falling back to simple demo");
    lvgl_demo_simple();

#else
    /* Fallback to simple demo */
    lvgl_demo_simple();
#endif

    ESP_LOGI(TAG, "Demo started successfully");
}

#endif /* CONFIG_BSP_LVGL_ENABLE_DEMO */
#endif /* CONFIG_BSP_ENABLE_LVGL */
