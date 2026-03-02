/*
 * SPDX-FileCopyrightText: 2024 Cristiano Gorla
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Guition JC1060P470C - LVGL Demo Applications Implementation
 */

#include "sdkconfig.h"

#ifdef CONFIG_BSP_ENABLE_LVGL

#include "lvgl_demo.h"
#include "bsp_lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

/* Include LVGL demos if available */
#if __has_include("lv_demos.h")
#include "lv_demos.h"
#define LVGL_DEMOS_AVAILABLE 1
#else
#define LVGL_DEMOS_AVAILABLE 0
#endif

static const char *TAG = "lvgl_demo";

/* Simple demo objects */
static lv_obj_t *simple_demo_screen = NULL;
static lv_obj_t *fps_label = NULL;
static lv_obj_t *touch_label = NULL;
static uint32_t frame_count = 0;
static uint64_t last_time = 0;

/**
 * @brief FPS counter timer callback
 */
static void fps_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    
    uint64_t now = esp_timer_get_time();
    uint64_t elapsed = now - last_time;
    
    if (elapsed >= 1000000) { // 1 second
        float fps = (float)frame_count * 1000000.0f / (float)elapsed;
        
        if (fps_label && bsp_lvgl_lock(10)) {
            lv_label_set_text_fmt(fps_label, "FPS: %.1f", fps);
            bsp_lvgl_unlock();
        }
        
        frame_count = 0;
        last_time = now;
    }
    frame_count++;
}

/**
 * @brief Touch event handler for simple demo
 */
static void touch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_active();
        if (indev) {
            lv_point_t point;
            lv_indev_get_point(indev, &point);
            
            if (touch_label) {
                lv_label_set_text_fmt(touch_label, "Touch: X=%d Y=%d", point.x, point.y);
            }
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (touch_label) {
            lv_label_set_text(touch_label, "Touch: Released");
        }
    }
}

esp_err_t lvgl_demo_simple(void)
{
    ESP_LOGI(TAG, "Starting simple LVGL demo");
    
    lv_display_t *display = bsp_lvgl_get_display();
    if (!display) {
        ESP_LOGE(TAG, "LVGL display not initialized");
        return ESP_FAIL;
    }

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return ESP_FAIL;
    }

    /* Create screen */
    simple_demo_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(simple_demo_screen, lv_color_hex(0x003a57), 0);
    
    /* Title label */
    lv_obj_t *title = lv_label_create(simple_demo_screen);
    lv_label_set_text(title, "Guition JC1060P470C");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);
    
    /* Subtitle */
    lv_obj_t *subtitle = lv_label_create(simple_demo_screen);
    lv_label_set_text(subtitle, "LVGL 9.2.2 Demo");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x00ff88), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 90);
    
    /* Display info */
    lv_obj_t *info = lv_label_create(simple_demo_screen);
    lv_label_set_text_fmt(info, "Resolution: 1024x600\nRotation: %d°\nColor: RGB565",
                          bsp_lvgl_get_rotation());
    lv_obj_set_style_text_font(info, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 150);
    
    /* FPS counter */
    fps_label = lv_label_create(simple_demo_screen);
    lv_label_set_text(fps_label, "FPS: --");
    lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0xffaa00), 0);
    lv_obj_align(fps_label, LV_ALIGN_TOP_LEFT, 20, 20);
    
    /* Touch info */
#ifdef CONFIG_BSP_LVGL_TOUCH_ENABLE
    touch_label = lv_label_create(simple_demo_screen);
    lv_label_set_text(touch_label, "Touch: None");
    lv_obj_set_style_text_font(touch_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(touch_label, lv_color_white(), 0);
    lv_obj_align(touch_label, LV_ALIGN_BOTTOM_MID, 0, -120);
#endif
    
    /* Touch test area */
    lv_obj_t *touch_area = lv_obj_create(simple_demo_screen);
    lv_obj_set_size(touch_area, 600, 120);
    lv_obj_align(touch_area, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(touch_area, lv_color_hex(0x206080), 0);
    lv_obj_set_style_border_color(touch_area, lv_color_hex(0x00ff88), 0);
    lv_obj_set_style_border_width(touch_area, 3, 0);
    lv_obj_set_style_radius(touch_area, 15, 0);
    lv_obj_add_flag(touch_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(touch_area, touch_event_cb, LV_EVENT_ALL, NULL);
    
    lv_obj_t *touch_hint = lv_label_create(touch_area);
    lv_label_set_text(touch_hint, "Touch Test Area - Tap anywhere");
    lv_obj_set_style_text_color(touch_hint, lv_color_white(), 0);
    lv_obj_center(touch_hint);
    
    /* Color gradient bar */
    lv_obj_t *gradient = lv_obj_create(simple_demo_screen);
    lv_obj_set_size(gradient, 800, 40);
    lv_obj_align(gradient, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_grad_color(gradient, lv_color_hex(0xff0000), 0);
    lv_obj_set_style_bg_grad_dir(gradient, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_color(gradient, lv_color_hex(0x0000ff), 0);
    lv_obj_set_style_radius(gradient, 20, 0);
    
    /* Load screen */
    lv_screen_load(simple_demo_screen);
    
    /* Start FPS timer */
    last_time = esp_timer_get_time();
    frame_count = 0;
    lv_timer_create(fps_timer_cb, 100, NULL);
    
    bsp_lvgl_unlock();
    
    ESP_LOGI(TAG, "Simple demo started successfully");
    return ESP_OK;
}

esp_err_t lvgl_demo_widgets(void)
{
#if LVGL_DEMOS_AVAILABLE && defined(LV_USE_DEMO_WIDGETS)
    ESP_LOGI(TAG, "Starting LVGL widgets demo");
    
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return ESP_FAIL;
    }
    
    lv_demo_widgets();
    
    bsp_lvgl_unlock();
    
    ESP_LOGI(TAG, "Widgets demo started");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "LVGL widgets demo not available (lv_demos not included)");
    ESP_LOGW(TAG, "Falling back to simple demo");
    return lvgl_demo_simple();
#endif
}

esp_err_t lvgl_demo_benchmark(void)
{
#if LVGL_DEMOS_AVAILABLE && defined(LV_USE_DEMO_BENCHMARK)
    ESP_LOGI(TAG, "Starting LVGL benchmark demo");
    
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return ESP_FAIL;
    }
    
    lv_demo_benchmark();
    
    bsp_lvgl_unlock();
    
    ESP_LOGI(TAG, "Benchmark demo started");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "LVGL benchmark demo not available (lv_demos not included)");
    ESP_LOGW(TAG, "Falling back to simple demo");
    return lvgl_demo_simple();
#endif
}

esp_err_t lvgl_demo_stress(void)
{
#if LVGL_DEMOS_AVAILABLE && defined(LV_USE_DEMO_STRESS)
    ESP_LOGI(TAG, "Starting LVGL stress test demo");
    
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return ESP_FAIL;
    }
    
    lv_demo_stress();
    
    bsp_lvgl_unlock();
    
    ESP_LOGI(TAG, "Stress test demo started");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "LVGL stress demo not available (lv_demos not included)");
    ESP_LOGW(TAG, "Falling back to simple demo");
    return lvgl_demo_simple();
#endif
}

esp_err_t lvgl_demo_stop(void)
{
    if (!bsp_lvgl_lock(1000)) {
        return ESP_FAIL;
    }
    
    if (simple_demo_screen) {
        lv_obj_del(simple_demo_screen);
        simple_demo_screen = NULL;
        fps_label = NULL;
        touch_label = NULL;
    }
    
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_screen_load(screen);
    
    bsp_lvgl_unlock();
    
    ESP_LOGI(TAG, "Demo stopped");
    return ESP_OK;
}

esp_err_t lvgl_demo_run(lvgl_demo_type_t demo)
{
    switch (demo) {
        case LVGL_DEMO_SIMPLE:
            return lvgl_demo_simple();
            
        case LVGL_DEMO_WIDGETS:
            return lvgl_demo_widgets();
            
        case LVGL_DEMO_BENCHMARK:
            return lvgl_demo_benchmark();
            
        case LVGL_DEMO_STRESS:
            return lvgl_demo_stress();
            
        case LVGL_DEMO_NONE:
            ESP_LOGI(TAG, "No demo selected");
            return ESP_OK;
            
        default:
            ESP_LOGE(TAG, "Invalid demo type: %d", demo);
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t lvgl_demo_run_from_config(void)
{
#ifdef CONFIG_BSP_LVGL_ENABLE_DEMO
    lvgl_demo_type_t demo = LVGL_DEMO_NONE;
    
#ifdef CONFIG_BSP_LVGL_DEMO_SIMPLE
    demo = LVGL_DEMO_SIMPLE;
    ESP_LOGI(TAG, "Auto-running: Simple demo (from Kconfig)");
#elif defined(CONFIG_BSP_LVGL_DEMO_WIDGETS)
    demo = LVGL_DEMO_WIDGETS;
    ESP_LOGI(TAG, "Auto-running: Widgets demo (from Kconfig)");
#elif defined(CONFIG_BSP_LVGL_DEMO_BENCHMARK)
    demo = LVGL_DEMO_BENCHMARK;
    ESP_LOGI(TAG, "Auto-running: Benchmark demo (from Kconfig)");
#elif defined(CONFIG_BSP_LVGL_DEMO_STRESS)
    demo = LVGL_DEMO_STRESS;
    ESP_LOGI(TAG, "Auto-running: Stress test demo (from Kconfig)");
#endif
    
    if (demo != LVGL_DEMO_NONE) {
        return lvgl_demo_run(demo);
    } else {
        ESP_LOGW(TAG, "Demo enabled but no demo type selected in Kconfig");
        return ESP_FAIL;
    }
#else
    ESP_LOGI(TAG, "Demos disabled (CONFIG_BSP_LVGL_ENABLE_DEMO not set)");
    return ESP_OK;
#endif
}

#endif /* CONFIG_BSP_ENABLE_LVGL */
