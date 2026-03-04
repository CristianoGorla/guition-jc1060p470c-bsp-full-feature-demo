/*
 * LVGL Initialization - Application Layer
 * Moved from BSP to keep hardware/software separation
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "lvgl_init.h"
#include "bsp_board.h"
#include "esp_log.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "sdkconfig.h"

static const char *TAG = "LVGL_INIT";

#ifndef CONFIG_BSP_LVGL_BUFFER_LINES
#define CONFIG_BSP_LVGL_BUFFER_LINES 100
#endif

#ifndef CONFIG_BSP_DISPLAY_WIDTH
#define CONFIG_BSP_DISPLAY_WIDTH 1024
#endif

// FIX: Single buffer to match num_fbs=1
#ifndef CONFIG_BSP_LVGL_DOUBLE_BUFFER
#define CONFIG_BSP_LVGL_DOUBLE_BUFFER 0
#endif

/* Global touch indev for debugging */
static lv_indev_t *g_touch_indev = NULL;

/**
 * @brief DSI color transfer done callback
 */
static bool on_color_trans_done(esp_lcd_panel_handle_t panel, 
                                 esp_lcd_dpi_panel_event_data_t *edata, 
                                 void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

/**
 * @brief Touch event debug callback (LVGL v9)
 */
static void touch_event_cb(lv_event_t *e)
{
    static uint32_t last_log_time = 0;
    uint32_t now = esp_log_timestamp();
    
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_PRESSED) {
        lv_point_t point;
        lv_indev_get_point(g_touch_indev, &point);
        ESP_LOGI(TAG, "🟢 Touch PRESSED at (%d, %d)", point.x, point.y);
    } else if (code == LV_EVENT_RELEASED) {
        if (now - last_log_time > 500) {
            ESP_LOGI(TAG, "🔴 Touch RELEASED");
            last_log_time = now;
        }
    }
}

esp_err_t lvgl_port_init_custom(void)
{
    esp_lcd_panel_handle_t display_handle = bsp_display_get_handle();
    esp_lcd_touch_handle_t touch_handle = bsp_touch_get_handle();
    
    if (!display_handle) {
        ESP_LOGE(TAG, "Display not initialized! Call bsp_board_init() first.");
        return ESP_FAIL;
    }
    
    if (!touch_handle) {
        ESP_LOGE(TAG, "Touch not initialized! Call bsp_board_init() first.");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Initializing LVGL port...");
    
    /* LVGL port configuration */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 6144,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));
    
    /* Calculate buffer size */
    const uint32_t buffer_pixels = CONFIG_BSP_DISPLAY_WIDTH * CONFIG_BSP_LVGL_BUFFER_LINES;
    
    /* Display configuration */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = NULL,
        .panel_handle = display_handle,
        .buffer_size = buffer_pixels,
        .double_buffer = CONFIG_BSP_LVGL_DOUBLE_BUFFER,
        .hres = 1024,
        .vres = 600,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { 
            .swap_xy = false,
            .mirror_x = false, 
            .mirror_y = false 
        },
        .flags = {
            .buff_dma = true,       // For DSI, always uses frame buffer from driver
            .buff_spiram = true,
            .sw_rotate = true,
        }
    };
    
    /* DSI-specific configuration */
    /* FIX: avoid_tearing=false allows single frame buffer (num_fbs=1)
     * This saves ~800KB memory vs num_fbs=2 required by avoid_tearing=true.
     * See docs/LVGL_DSI_CONFIGURATION.md for details.
     */
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = false,  // Changed from true - allows num_fbs=1
        }
    };
    
    ESP_LOGI(TAG, "Adding display (1024x600, %dx%d buffer = %u pixels)",
             CONFIG_BSP_DISPLAY_WIDTH, CONFIG_BSP_LVGL_BUFFER_LINES, buffer_pixels);
    
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "Failed to add LVGL display!");
        return ESP_FAIL;
    }

    /* Register DSI flush callback */
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = on_color_trans_done,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(display_handle, &cbs, disp));
    ESP_LOGI(TAG, "DSI flush callback registered");
    
    /* Add touch with error checking */
    ESP_LOGI(TAG, "Adding touch...");
    const lvgl_port_touch_cfg_t touch_cfg = { 
        .disp = disp, 
        .handle = touch_handle 
    };
    
    g_touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (!g_touch_indev) {
        ESP_LOGE(TAG, "❌ Failed to add touch to LVGL!");
        return ESP_FAIL;
    }
    
    /* Verify touch device type */
    lv_indev_type_t indev_type = lv_indev_get_type(g_touch_indev);
    ESP_LOGI(TAG, "✅ Touch added successfully, type: %d (expected 1=POINTER)", indev_type);
    ESP_LOGI(TAG, "Touch will be monitored via event callbacks");
    
    /* Create an invisible object covering entire screen for touch event monitoring */
    lv_obj_t *touch_monitor = lv_obj_create(lv_screen_active());
    lv_obj_set_size(touch_monitor, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(touch_monitor, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch_monitor, 0, 0);
    lv_obj_add_flag(touch_monitor, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(touch_monitor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(touch_monitor, touch_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(touch_monitor, touch_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_move_background(touch_monitor);
    
    ESP_LOGI(TAG, "Touch debug monitor created (transparent background layer)");
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ✓ LVGL Ready (1024x600)");
    ESP_LOGI(TAG, "  Buffer: %dx%d (%.1f KB, %s, HW FB)",
             CONFIG_BSP_DISPLAY_WIDTH, CONFIG_BSP_LVGL_BUFFER_LINES,
             (buffer_pixels * 2) / 1024.0f,
             CONFIG_BSP_LVGL_DOUBLE_BUFFER ? "double" : "single");
    ESP_LOGI(TAG, "  Touch: type %d, event monitoring enabled", indev_type);
    ESP_LOGI(TAG, "========================================");
    
    return ESP_OK;
}
