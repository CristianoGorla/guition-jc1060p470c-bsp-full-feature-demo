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

/* VENDOR CONFIGURATION - Proven working values from vendor demo */
#define BSP_LCD_H_RES 1024
#define BSP_LCD_V_RES 600
#define BSP_LCD_DRAW_BUFF_SIZE     (BSP_LCD_H_RES * 50)  // 51200 pixels = 100KB
#define BSP_LCD_DRAW_BUFF_DOUBLE   (0)                   // Single buffer only

static uint32_t g_flush_count = 0;

/**
 * @brief DSI color transfer done callback
 */
static bool on_color_trans_done(esp_lcd_panel_handle_t panel, 
                                 esp_lcd_dpi_panel_event_data_t *edata, 
                                 void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    
    g_flush_count++;
    
    /* Log every 100 flushes to verify callback is working */
    if (g_flush_count % 100 == 0) {
        ESP_LOGI(TAG, "📺 DSI flush callback called (count: %lu)", g_flush_count);
    }
    
    lv_display_flush_ready(disp);
    return false;
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
    
    /* Display configuration - MATCH WORKING DEMO */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = NULL,
        .panel_handle = display_handle,
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,        // 51200 pixels (vendor value)
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,    // false (vendor value)
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { 
            .swap_xy = true,   // VENDOR: true for landscape mode
            .mirror_x = false, 
            .mirror_y = false 
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,    // FIXED: Use PSRAM like working demo
            .sw_rotate = true,
        }
    };
    
    /* DSI-specific configuration - MATCH VENDOR */
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = false,  // VENDOR: disabled (simpler, no overhead)
        }
    };
    
    ESP_LOGI(TAG, "Adding display (%dx%d, %dx50 buffer = %u pixels)", 
             BSP_LCD_H_RES, BSP_LCD_V_RES, BSP_LCD_H_RES, BSP_LCD_DRAW_BUFF_SIZE);
    
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
    ESP_LOGI(TAG, "DSI flush callback registered (will log every 100 flushes)");
    
    /* Register touch input */
    ESP_LOGI(TAG, "Registering touch input via esp_lvgl_port helper...");
    
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = touch_handle,
    };
    
    lv_indev_t *touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (!touch_indev) {
        ESP_LOGE(TAG, "❌ Failed to add touch input device!");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✅ Touch registered via lvgl_port_add_touch (automatic polling)");
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ✓ LVGL Ready (1024x600)");
    ESP_LOGI(TAG, "  Buffer: 1024x50 (%.1f KB, %s, PSRAM)",
             (BSP_LCD_DRAW_BUFF_SIZE * 2) / 1024.0f,
             BSP_LCD_DRAW_BUFF_DOUBLE ? "double" : "single");
    ESP_LOGI(TAG, "  Touch: esp_lvgl_port helper (timer-based polling)");
    ESP_LOGI(TAG, "  DSI: avoid_tearing=false (vendor config)");
    ESP_LOGI(TAG, "  Rotation: swap_xy=true (landscape mode)");
    ESP_LOGI(TAG, "  Flush callback: ACTIVE (monitoring enabled)");
    ESP_LOGI(TAG, "========================================");
    
    return ESP_OK;
}
