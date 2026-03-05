/*
 * SPDX-FileCopyrightText: 2024 Cristiano Gorla
 * SPDX-License-Identifier: Apache-2.0
 *
 * Guition JC1060P470C - LVGL Demo Applications Implementation
 */

#include "sdkconfig.h"

#ifdef CONFIG_BSP_ENABLE_LVGL

#include "lvgl_demo.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

/* Include LVGL demos if available */
#if __has_include("lv_demos.h")
#include "lv_demos.h"
#define LVGL_DEMOS_AVAILABLE 1
#else
#define LVGL_DEMOS_AVAILABLE 0
#endif

static const char *TAG = "lvgl_demo";

/* Custom demo objects */
static lv_obj_t *demo_screen = NULL;
static lv_obj_t *fps_label = NULL;
static lv_obj_t *coord_label = NULL;
static lv_obj_t *slider_label = NULL;
static lv_obj_t *button_label = NULL;
static lv_obj_t *center_label = NULL;     // Center button label (HW test only)
static lv_obj_t *crosshair_h = NULL;      // Horizontal line (HW test only)
static lv_obj_t *crosshair_v = NULL;      // Vertical line (HW test only)
static lv_obj_t *crosshair_center = NULL; // Center dot (HW test only)
static uint32_t frame_count = 0;
static uint64_t last_time = 0;
static uint32_t button_clicks = 0;
static uint32_t center_clicks = 0;

/**
 * @brief FPS counter timer callback
 */
static void fps_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    uint64_t now = esp_timer_get_time();
    uint64_t elapsed = now - last_time;

    if (elapsed >= 1000000)
    { // 1 second
        float fps = (float)frame_count * 1000000.0f / (float)elapsed;

        if (fps_label)
        {
            lv_label_set_text_fmt(fps_label, "FPS: %.1f", fps);
        }

        frame_count = 0;
        last_time = now;
    }
    frame_count++;
}

/**
 * @brief Update crosshair position (HW test only)
 */
static void update_crosshair(int32_t x, int32_t y)
{
    if (!crosshair_h || !crosshair_v || !crosshair_center)
        return;

    // Update horizontal line (full width)
    lv_obj_set_pos(crosshair_h, 0, y - 1);

    // Update vertical line (full height)
    lv_obj_set_pos(crosshair_v, x - 1, 0);

    // Update center dot
    lv_obj_set_pos(crosshair_center, x - 5, y - 5);

    // Update coordinate label
    if (coord_label)
    {
        lv_label_set_text_fmt(coord_label, "Touch: X=%d Y=%d", (int)x, (int)y);
    }
}

/**
 * @brief Touch event handler for screen (HW test only)
 */
static void screen_touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING)
    {
        lv_indev_t *indev = lv_indev_active();
        if (indev)
        {
            lv_point_t point;
            lv_indev_get_point(indev, &point);
            update_crosshair(point.x, point.y);

            // Show crosshair
            if (crosshair_h)
                lv_obj_clear_flag(crosshair_h, LV_OBJ_FLAG_HIDDEN);
            if (crosshair_v)
                lv_obj_clear_flag(crosshair_v, LV_OBJ_FLAG_HIDDEN);
            if (crosshair_center)
                lv_obj_clear_flag(crosshair_center, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else if (code == LV_EVENT_RELEASED)
    {
        // Hide crosshair on release
        if (crosshair_h)
            lv_obj_add_flag(crosshair_h, LV_OBJ_FLAG_HIDDEN);
        if (crosshair_v)
            lv_obj_add_flag(crosshair_v, LV_OBJ_FLAG_HIDDEN);
        if (crosshair_center)
            lv_obj_add_flag(crosshair_center, LV_OBJ_FLAG_HIDDEN);

        if (coord_label)
        {
            lv_label_set_text(coord_label, "Touch: ---");
        }
    }
}

/**
 * @brief Slider event handler
 */
static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);

    if (slider_label)
    {
        lv_label_set_text_fmt(slider_label, "Slider: %d%%", (int)value);
    }
}

/**
 * @brief Button event handler
 */
static void button_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        button_clicks++;
        if (button_label)
        {
            lv_label_set_text_fmt(button_label, "Clicks: %lu", button_clicks);
        }
    }
}

/**
 * @brief Center button event handler (HW test only)
 */
static void center_button_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        center_clicks++;
        if (center_label)
        {
            lv_label_set_text_fmt(center_label, "CENTER\n%lu", center_clicks);
        }
        ESP_LOGI(TAG, "Center button clicked (count: %lu)", center_clicks);
    }
}

/**
 * @brief Simple UI Demo (Default)
 * Minimal UI with basic controls for testing display/touch
 */
esp_err_t lvgl_demo_simple(void)
{
    ESP_LOGI(TAG, "Starting simple UI demo...");

    if (!lvgl_port_lock(portMAX_DELAY))
    {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return ESP_FAIL;
    }

    /* Create screen */
    demo_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(demo_screen, lv_color_hex(0x1a1a2e), 0);

    /* Title */
    lv_obj_t *title = lv_label_create(demo_screen);
    lv_label_set_text(title, "Guition JC1060P470C");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d9ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    /* Subtitle */
    lv_obj_t *subtitle = lv_label_create(demo_screen);
    lv_label_set_text(subtitle, "1024x600 MIPI-DSI Display + GT911 Touch");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x888888), 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 70);

    /* FPS counter */
    fps_label = lv_label_create(demo_screen);
    lv_label_set_text(fps_label, "FPS: --");
    lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0xffa500), 0);
    lv_obj_align(fps_label, LV_ALIGN_TOP_LEFT, 15, 15);

    /* Interactive controls */
    lv_obj_t *controls = lv_obj_create(demo_screen);
    lv_obj_set_size(controls, 700, 250);
    lv_obj_align(controls, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_bg_color(controls, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(controls, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(controls, 2, 0);
    lv_obj_set_style_radius(controls, 15, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(controls, 30, 0);
    lv_obj_set_style_pad_row(controls, 30, 0);

    /* Slider */
    lv_obj_t *slider_container = lv_obj_create(controls);
    lv_obj_set_size(slider_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(slider_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(slider_container, 0, 0);
    lv_obj_set_style_pad_all(slider_container, 0, 0);

    slider_label = lv_label_create(slider_container);
    lv_label_set_text(slider_label, "Slider: 50%");
    lv_obj_set_style_text_font(slider_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(slider_label, lv_color_white(), 0);
    lv_obj_align(slider_label, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *slider = lv_slider_create(slider_container);
    lv_obj_set_size(slider, 600, 14);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 40);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x4a4a6a), 0);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x00d9ff), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xffffff), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Button */
    lv_obj_t *btn = lv_button_create(controls);
    lv_obj_set_size(btn, 250, 60);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16b1ff), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_text = lv_label_create(btn);
    lv_label_set_text(btn_text, "Click Me!");
    lv_obj_set_style_text_font(btn_text, &lv_font_montserrat_22, 0);
    lv_obj_center(btn_text);

    button_label = lv_label_create(controls);
    lv_label_set_text(button_label, "Clicks: 0");
    lv_obj_set_style_text_font(button_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(button_label, lv_color_hex(0xffaa00), 0);

    /* Instructions */
    lv_obj_t *instructions = lv_label_create(demo_screen);
    lv_label_set_text(instructions, "Touch controls to test display and touch input");
    lv_obj_set_style_text_font(instructions, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(instructions, lv_color_hex(0x666666), 0);
    lv_obj_align(instructions, LV_ALIGN_BOTTOM_MID, 0, -15);

    /* Load screen */
    lv_screen_load(demo_screen);

    /* Start FPS timer */
    last_time = esp_timer_get_time();
    frame_count = 0;
    button_clicks = 0;
    lv_timer_create(fps_timer_cb, 100, NULL);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Simple UI demo started");
    return ESP_OK;
}

/**
 * @brief Hardware Test UI Demo
 * Precise center button (512,300) + crosshair for touch calibration
 */
esp_err_t lvgl_demo_hw_test(void)
{
    ESP_LOGI(TAG, "Starting hardware test UI demo...");

    if (!lvgl_port_lock(portMAX_DELAY))
    {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return ESP_FAIL;
    }

    /* Create screen */
    demo_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(demo_screen, lv_color_hex(0x1a1a2e), 0);

    /* Title */
    lv_obj_t *title = lv_label_create(demo_screen);
    lv_label_set_text(title, "Guition JC1060P470C - Touch Calibration Test");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00d9ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    /* FPS counter (top-left) */
    fps_label = lv_label_create(demo_screen);
    lv_label_set_text(fps_label, "FPS: --");
    lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0xffa500), 0);
    lv_obj_align(fps_label, LV_ALIGN_TOP_LEFT, 15, 15);

    /* Coordinate display (top-right) */
    coord_label = lv_label_create(demo_screen);
    lv_label_set_text(coord_label, "Touch: ---");
    lv_obj_set_style_text_font(coord_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(coord_label, lv_color_hex(0x00ff88), 0);
    lv_obj_align(coord_label, LV_ALIGN_TOP_RIGHT, -15, 15);

    /* CENTER TEST BUTTON (512, 300) */
    lv_obj_t *center_btn = lv_button_create(demo_screen);
    lv_obj_set_size(center_btn, 200, 200);
    lv_obj_set_pos(center_btn, 512 - 100, 300 - 100); // Center at (512, 300)
    lv_obj_set_style_bg_color(center_btn, lv_color_hex(0xff0000), 0);
    lv_obj_set_style_bg_color(center_btn, lv_color_hex(0xff6666), LV_STATE_PRESSED);
    lv_obj_set_style_radius(center_btn, 20, 0);
    lv_obj_set_style_border_color(center_btn, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(center_btn, 4, 0);
    lv_obj_add_event_cb(center_btn, center_button_event_cb, LV_EVENT_CLICKED, NULL);

    center_label = lv_label_create(center_btn);
    lv_label_set_text(center_label, "CENTER\n0");
    lv_obj_set_style_text_font(center_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(center_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(center_label);

    ESP_LOGI(TAG, "Center button created at position (412, 200), size 200x200");
    ESP_LOGI(TAG, "Center button exact center: (512, 300)");

    /* CROSSHAIR (initially hidden) */

    /* Horizontal line */
    crosshair_h = lv_obj_create(demo_screen);
    lv_obj_set_size(crosshair_h, 1024, 2);
    lv_obj_set_style_bg_color(crosshair_h, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_bg_opa(crosshair_h, LV_OPA_70, 0);
    lv_obj_set_style_border_width(crosshair_h, 0, 0);
    lv_obj_clear_flag(crosshair_h, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(crosshair_h, LV_OBJ_FLAG_HIDDEN);

    /* Vertical line */
    crosshair_v = lv_obj_create(demo_screen);
    lv_obj_set_size(crosshair_v, 2, 600);
    lv_obj_set_style_bg_color(crosshair_v, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_bg_opa(crosshair_v, LV_OPA_70, 0);
    lv_obj_set_style_border_width(crosshair_v, 0, 0);
    lv_obj_clear_flag(crosshair_v, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(crosshair_v, LV_OBJ_FLAG_HIDDEN);

    /* Center dot */
    crosshair_center = lv_obj_create(demo_screen);
    lv_obj_set_size(crosshair_center, 10, 10);
    lv_obj_set_style_bg_color(crosshair_center, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_radius(crosshair_center, 5, 0);
    lv_obj_set_style_border_width(crosshair_center, 0, 0);
    lv_obj_clear_flag(crosshair_center, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(crosshair_center, LV_OBJ_FLAG_HIDDEN);

    /* Interactive controls */
    lv_obj_t *controls = lv_obj_create(demo_screen);
    lv_obj_set_size(controls, 700, 180);
    lv_obj_align(controls, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_bg_color(controls, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(controls, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(controls, 2, 0);
    lv_obj_set_style_radius(controls, 15, 0);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(controls, 20, 0);
    lv_obj_set_style_pad_row(controls, 20, 0);

    /* Slider */
    lv_obj_t *slider_container = lv_obj_create(controls);
    lv_obj_set_size(slider_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(slider_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(slider_container, 0, 0);
    lv_obj_set_style_pad_all(slider_container, 0, 0);

    slider_label = lv_label_create(slider_container);
    lv_label_set_text(slider_label, "Slider: 50%");
    lv_obj_set_style_text_font(slider_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(slider_label, lv_color_white(), 0);
    lv_obj_align(slider_label, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *slider = lv_slider_create(slider_container);
    lv_obj_set_size(slider, 600, 12);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 35);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x4a4a6a), 0);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x00d9ff), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xffffff), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Button */
    lv_obj_t *btn = lv_button_create(controls);
    lv_obj_set_size(btn, 200, 50);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x16b1ff), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_text = lv_label_create(btn);
    lv_label_set_text(btn_text, "Click Me!");
    lv_obj_set_style_text_font(btn_text, &lv_font_montserrat_20, 0);
    lv_obj_center(btn_text);

    button_label = lv_label_create(controls);
    lv_label_set_text(button_label, "Clicks: 0");
    lv_obj_set_style_text_font(button_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(button_label, lv_color_hex(0xffaa00), 0);

    /* Instructions */
    lv_obj_t *instructions = lv_label_create(demo_screen);
    lv_label_set_text(instructions,
                      "Touch RED center button (512,300) | Touch anywhere for green crosshair");
    lv_obj_set_style_text_font(instructions, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(instructions, lv_color_hex(0x888888), 0);
    lv_obj_align(instructions, LV_ALIGN_BOTTOM_MID, 0, -15);

    /* Touch event handler for entire screen */
    lv_obj_add_event_cb(demo_screen, screen_touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(demo_screen, screen_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(demo_screen, screen_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_flag(demo_screen, LV_OBJ_FLAG_CLICKABLE);

    /* Load screen */
    lv_screen_load(demo_screen);

    /* Start FPS timer */
    last_time = esp_timer_get_time();
    frame_count = 0;
    button_clicks = 0;
    center_clicks = 0;
    lv_timer_create(fps_timer_cb, 100, NULL);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Hardware test UI demo started");
    ESP_LOGI(TAG, "  Center button: 200x200 at exact center (512, 300)");
    ESP_LOGI(TAG, "  Green crosshair: Touch screen to visualize coordinates");
    ESP_LOGI(TAG, "  Test controls: Slider and click counter");
    return ESP_OK;
}

esp_err_t lvgl_demo_widgets(void)
{
#if LVGL_DEMOS_AVAILABLE && defined(LV_USE_DEMO_WIDGETS)
    ESP_LOGI(TAG, "Starting LVGL widgets demo");

    if (!lvgl_port_lock(portMAX_DELAY))
    {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return ESP_FAIL;
    }

    lv_demo_widgets();

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Widgets demo started");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "LVGL widgets demo not available (lv_demos not included)");
    ESP_LOGW(TAG, "Falling back to simple UI demo");
    return lvgl_demo_simple();
#endif
}

esp_err_t lvgl_demo_stop(void)
{
    if (!lvgl_port_lock(portMAX_DELAY))
    {
        return ESP_FAIL;
    }

    if (demo_screen)
    {
        lv_obj_del(demo_screen);
        demo_screen = NULL;
        fps_label = NULL;
        coord_label = NULL;
        slider_label = NULL;
        button_label = NULL;
        center_label = NULL;
        crosshair_h = NULL;
        crosshair_v = NULL;
        crosshair_center = NULL;
    }

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_screen_load(screen);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Demo stopped");
    return ESP_OK;
}

esp_err_t lvgl_demo_run_from_config(void)
{
#ifdef CONFIG_LVGL_DEMO_SIMPLE_UI
    ESP_LOGI(TAG, "Config: Simple UI (default)");
    return lvgl_demo_simple();
#elif defined(CONFIG_LVGL_DEMO_HW_TEST)
    ESP_LOGI(TAG, "Config: Hardware Test UI");
    return lvgl_demo_hw_test();
#elif defined(CONFIG_LVGL_DEMO_WIDGETS)
    ESP_LOGI(TAG, "Config: LVGL Widgets Demo");
    return lvgl_demo_widgets();
#else
    ESP_LOGW(TAG, "No demo selected in menuconfig, using Simple UI");
    return lvgl_demo_simple();
#endif
}

#endif /* CONFIG_BSP_ENABLE_LVGL */
