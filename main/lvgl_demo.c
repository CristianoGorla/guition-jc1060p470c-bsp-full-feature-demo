/**
 * @file lvgl_demo.c
 * @brief LVGL Demo Application for Guition JC1060P470C
 * 
 * Provides multiple demo modes:
 * - Simple test: Basic label and button
 * - Widgets demo: Full LVGL widgets showcase
 * - Benchmark: Performance testing
 * - Stress test: System stability testing
 */

#include "lvgl_demo.h"
#include "feature_flags.h"
#include "esp_log.h"
#include "bsp_lvgl.h"
#include "lvgl.h"

#if ENABLE_LVGL && ENABLE_LVGL_DEMO

/* Include LVGL demos if available */
#ifdef LV_USE_DEMO_WIDGETS
#include "demos/lv_demos.h"
#endif

static const char *TAG = "LVGL_DEMO";

/**
 * @brief Simple LVGL test screen
 * 
 * Creates a basic UI with:
 * - Title label
 * - Info labels (resolution, touch)
 * - Test button with click handler
 * - FPS counter
 */
static void lvgl_demo_simple(void)
{
    ESP_LOGI(TAG, "Creating simple test screen...");

    /* Create main container */
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x003a57), LV_PART_MAIN);

    /* Title label */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Guition JC1060P470C");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    /* Subtitle */
    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "LVGL 9.2.2 Test");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x00d9ff), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 80);

    /* Display info */
    lv_obj_t *info = lv_label_create(scr);
    lv_label_set_text_fmt(info, "Display: 1024x600 @ RGB565\nTouch: GT911 Multi-touch\nDSI: 2-lane @ 750 Mbps");
    lv_obj_set_style_text_color(info, lv_color_white(), 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -20);

    /* Test button */
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x00d9ff), LV_PART_MAIN);

    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Touch Me!");
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0x003a57), 0);
    lv_obj_center(btn_label);

    /* FPS counter */
    lv_obj_t *fps_label = lv_label_create(scr);
    lv_label_set_text(fps_label, "FPS: --");
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0x00d9ff), 0);
    lv_obj_align(fps_label, LV_ALIGN_BOTTOM_RIGHT, -10, -10);

    /* Status text */
    lv_obj_t *status = lv_label_create(scr);
    lv_label_set_text(status, "✓ LVGL initialized successfully!");
    lv_obj_set_style_text_color(status, lv_color_hex(0x00ff00), 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_MID, 0, -10);

    ESP_LOGI(TAG, "✓ Simple test screen created");
}

/**
 * @brief Initialize and run LVGL demo
 */
void lvgl_demo_run(void)
{
    ESP_LOGI(TAG, "========== LVGL Demo Start ==========");
    ESP_LOGI(TAG, "Demo type: %d", LVGL_DEMO_TYPE);

#if LVGL_DEMO_TYPE == 0
    /* Simple test screen */
    ESP_LOGI(TAG, "Running: Simple test screen");
    lvgl_demo_simple();

#elif LVGL_DEMO_TYPE == 1
    /* Widgets demo */
    ESP_LOGI(TAG, "Running: LVGL Widgets Demo");
#ifdef LV_USE_DEMO_WIDGETS
    lv_demo_widgets();
#else
    ESP_LOGW(TAG, "Widgets demo not available, showing simple screen");
    lvgl_demo_simple();
#endif

#elif LVGL_DEMO_TYPE == 2
    /* Benchmark demo */
    ESP_LOGI(TAG, "Running: LVGL Benchmark Demo");
#ifdef LV_USE_DEMO_BENCHMARK
    lv_demo_benchmark();
#else
    ESP_LOGW(TAG, "Benchmark demo not available, showing simple screen");
    lvgl_demo_simple();
#endif

#elif LVGL_DEMO_TYPE == 3
    /* Stress test demo */
    ESP_LOGI(TAG, "Running: LVGL Stress Test Demo");
#ifdef LV_USE_DEMO_STRESS
    lv_demo_stress();
#else
    ESP_LOGW(TAG, "Stress test demo not available, showing simple screen");
    lvgl_demo_simple();
#endif

#else
    /* Default: simple test */
    ESP_LOGW(TAG, "Unknown demo type %d, showing simple screen", LVGL_DEMO_TYPE);
    lvgl_demo_simple();
#endif

    ESP_LOGI(TAG, "========================================");
}

#endif // ENABLE_LVGL && ENABLE_LVGL_DEMO
