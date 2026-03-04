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

#ifndef CONFIG_BSP_LVGL_DOUBLE_BUFFER
#define CONFIG_BSP_LVGL_DOUBLE_BUFFER 1
#endif

/**
 * @brief VSYNC event callback for display synchronization
 * 
 * Called at vertical blanking interval when display is ready for next frame.
 * Ensures tear-free rendering by synchronizing LVGL refresh with display hardware.
 */
static bool on_vsync_event(esp_lcd_panel_handle_t panel, 
                           const esp_lcd_dpi_panel_event_data_t *edata, 
                           void *user_ctx)
{
    BaseType_t need_yield = pdFALSE;
    if (user_ctx) {
        lv_display_t *disp = (lv_display_t *)user_ctx;
        // Trigger LVGL to start next frame rendering
        need_yield = lv_display_flush_is_last(disp) ? pdTRUE : pdFALSE;
    }
    return need_yield;
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
            .buff_dma = false,      // FIX: Changed from true - MIPI-DSI doesn't work with DMA buffers
            .buff_spiram = true,    // Keep PSRAM allocation
            .sw_rotate = true,      // Keep software rotation support
        }
    };
    
    /* DSI-specific configuration */
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = true,
        }
    };
    
    ESP_LOGI(TAG, "Adding display (1024x600, %dx%d buffer = %u pixels)",
             CONFIG_BSP_DISPLAY_WIDTH, CONFIG_BSP_LVGL_BUFFER_LINES, buffer_pixels);
    
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "Failed to add LVGL display!");
        return ESP_FAIL;
    }

    /* Register VSYNC callback for display synchronization */
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_vsync = on_vsync_event,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(display_handle, &cbs, disp));
    ESP_LOGI(TAG, "VSYNC callback registered - tear-free rendering enabled");
    
    /* Add touch */
    ESP_LOGI(TAG, "Adding touch...");
    const lvgl_port_touch_cfg_t touch_cfg = { 
        .disp = disp, 
        .handle = touch_handle 
    };
    lvgl_port_add_touch(&touch_cfg);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ✓ LVGL Ready (1024x600)");
    ESP_LOGI(TAG, "  Buffer: %dx%d (%.1f KB, %s)",
             CONFIG_BSP_DISPLAY_WIDTH, CONFIG_BSP_LVGL_BUFFER_LINES,
             (buffer_pixels * 2) / 1024.0f,
             CONFIG_BSP_LVGL_DOUBLE_BUFFER ? "double" : "single");
    ESP_LOGI(TAG, "========================================");
    
    return ESP_OK;
}
