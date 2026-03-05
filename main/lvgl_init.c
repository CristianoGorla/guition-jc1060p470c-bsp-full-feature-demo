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
#include "esp_lcd_touch.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "sdkconfig.h"

static const char *TAG = "LVGL_INIT";
static const char *TOUCH_TAG = "TOUCH_DEBUG";

/* VENDOR CONFIGURATION - Exact match with GUITION-LVGL-V9-DEMO-ESP32P4 */
#define BSP_LCD_H_RES 1024
#define BSP_LCD_V_RES 600
#define BSP_LCD_DRAW_BUFF_SIZE     (480 * 800)  // 384000 pixels = 750KB (EXACT VENDOR VALUE)
#define BSP_LCD_DRAW_BUFF_DOUBLE   (0)          // Single buffer only

static uint32_t g_flush_count = 0;
static lv_indev_read_cb_t original_touch_read_cb = NULL;  // Original callback from esp_lvgl_port

/**
 * @brief DSI color transfer done callback
 * @note NO LOGGING from ISR context to prevent lock crashes
 */
static bool on_color_trans_done(esp_lcd_panel_handle_t panel, 
                                 esp_lcd_dpi_panel_event_data_t *edata, 
                                 void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    g_flush_count++;
    lv_display_flush_ready(disp);
    return false;
}

/**
 * @brief Debug touch wrapper callback - calls original callback then logs
 */
static void debug_touch_wrapper_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    // 1. Call the original esp_lvgl_port callback (does the real work: reads touch HW)
    if (original_touch_read_cb) {
        original_touch_read_cb(indev, data);
    }
    
    // 2. Log what LVGL received after esp_lvgl_port processing
    static lv_indev_state_t last_state = LV_INDEV_STATE_RELEASED;
    
    if (data->state == LV_INDEV_STATE_PRESSED) {
        // Only log on first press or coordinate changes
        if (last_state == LV_INDEV_STATE_RELEASED) {
            ESP_LOGI(TOUCH_TAG, "[PRESSED] LVGL received: X=%d Y=%d (screen=%dx%d)",
                     data->point.x, data->point.y, BSP_LCD_H_RES, BSP_LCD_V_RES);
        }
        last_state = LV_INDEV_STATE_PRESSED;
    } else if (data->state == LV_INDEV_STATE_RELEASED) {
        if (last_state == LV_INDEV_STATE_PRESSED) {
            ESP_LOGI(TOUCH_TAG, "[RELEASED]");
        }
        last_state = LV_INDEV_STATE_RELEASED;
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
    
    ESP_LOGI(TAG, "Initializing LVGL port (VENDOR CONFIG)...");
    
    /* LVGL port configuration */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 6144,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));
    
    /* Display configuration - EXACT VENDOR MATCH */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = NULL,
        .panel_handle = display_handle,
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { 
            .swap_xy = true,
            .mirror_x = false, 
            .mirror_y = false 
        },
        .flags = {
            .buff_dma = false,      // VENDOR: false (critical for PSRAM compatibility)
            .buff_spiram = true,    // VENDOR: true
            .sw_rotate = true,      // VENDOR: true
        }
    };
    
    /* DSI-specific configuration - VENDOR MATCH */
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = false,  // VENDOR: false
        }
    };
    
    ESP_LOGI(TAG, "Adding display (%dx%d, buffer=%u pixels = %.1f KB)", 
             BSP_LCD_H_RES, BSP_LCD_V_RES, BSP_LCD_DRAW_BUFF_SIZE,
             (BSP_LCD_DRAW_BUFF_SIZE * 2) / 1024.0f);
    
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "Failed to add LVGL display!");
        return ESP_FAIL;
    }

    /* Register DSI flush callback (no logging from ISR) */
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = on_color_trans_done,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(display_handle, &cbs, disp));
    ESP_LOGI(TAG, "DSI flush callback registered (silent mode)");
    
    /* Register touch input - EXACT VENDOR APPROACH */
    ESP_LOGI(TAG, "Registering touch input via esp_lvgl_port helper...");
    
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = touch_handle,
    };
    
    lv_indev_t *touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (!touch_indev) {
        ESP_LOGE(TAG, "[ERROR] Failed to add touch input device!");
        return ESP_FAIL;
    }
    
    /* Save original callback from esp_lvgl_port */
    original_touch_read_cb = lv_indev_get_read_cb(touch_indev);
    if (!original_touch_read_cb) {
        ESP_LOGW(TAG, "[WARN] Could not get original touch callback");
    } else {
        ESP_LOGI(TAG, "[OK] Original touch callback saved: %p", original_touch_read_cb);
    }
    
    /* Install wrapper callback that calls original + logs */
    lv_indev_set_read_cb(touch_indev, debug_touch_wrapper_cb);
    ESP_LOGI(TAG, "[OK] Debug wrapper callback installed (preserves polling)");
    ESP_LOGI(TAG, "[OK] Touch registered via lvgl_port_add_touch (automatic polling)");
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  LVGL Ready (1024x600)");
    ESP_LOGI(TAG, "  Buffer: 480x800 (%.1f KB, %s, PSRAM)",
             (BSP_LCD_DRAW_BUFF_SIZE * 2) / 1024.0f,
             BSP_LCD_DRAW_BUFF_DOUBLE ? "double" : "single");
    ESP_LOGI(TAG, "  Touch: esp_lvgl_port (auto-rotation via sw_rotate)");
    ESP_LOGI(TAG, "  Touch debug: WRAPPER mode (logs LVGL input)");
    ESP_LOGI(TAG, "  Config: buff_dma=false, buff_spiram=true");
    ESP_LOGI(TAG, "  DSI: avoid_tearing=false (vendor config)");
    ESP_LOGI(TAG, "  Rotation: swap_xy=true (landscape mode)");
    ESP_LOGI(TAG, "========================================");
    
    return ESP_OK;
}
