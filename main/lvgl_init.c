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

/* Global touch context */
typedef struct {
    esp_lcd_touch_handle_t touch_handle;
    bool was_pressed;
    uint16_t last_x;
    uint16_t last_y;
    uint32_t poll_count;
    uint32_t touch_count;
} touch_ctx_t;

static touch_ctx_t g_touch_ctx = {0};
static lv_indev_t *g_touch_indev = NULL;
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

/**
 * @brief Touch event debug callback (LVGL v9)
 */
static void touch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_PRESSED) {
        lv_point_t point;
        lv_indev_get_point(g_touch_indev, &point);
        ESP_LOGI(TAG, "🎯 Widget received PRESSED event at (%d, %d)", point.x, point.y);
    } else if (code == LV_EVENT_RELEASED) {
        ESP_LOGI(TAG, "🎯 Widget received RELEASED event");
    }
}

/**
 * @brief Custom LVGL indev read callback
 * 
 * This is called by LVGL to read touch state. We poll GT911 directly here
 * instead of relying on esp_lvgl_port automatic polling which may be broken
 * with avoid_tearing=false.
 * 
 * CRITICAL: NO ESP_LOGI() CALLS ALLOWED HERE!
 * This callback runs in LVGL task context which holds mutexes.
 * Logging would cause deadlock in newlib printf().
 */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    touch_ctx_t *ctx = &g_touch_ctx;
    
    ctx->poll_count++;
    
    /* Trigger GT911 read - this updates internal state */
    esp_err_t ret = esp_lcd_touch_read_data(ctx->touch_handle);
    
    if (ret == ESP_OK) {
        /* Get touch points from GT911 using 1.1.2 API */
        uint16_t x[1], y[1], strength[1];
        uint8_t touch_cnt = 0;
        
        bool touched = false;
        /* NOTE: esp_lcd_touch_get_coordinates is the correct API for version 1.1.2 */
        if (esp_lcd_touch_get_coordinates(ctx->touch_handle, x, y, strength, &touch_cnt, 1)) {
            touched = (touch_cnt > 0);
        }
        
        if (touched) {
            /* Touch detected */
            if (!ctx->was_pressed) {
                ctx->touch_count++;
                ctx->was_pressed = true;
            }
            
            /* Update LVGL data structure */
            data->state = LV_INDEV_STATE_PRESSED;
            data->point.x = x[0];
            data->point.y = y[0];
            
            ctx->last_x = x[0];
            ctx->last_y = y[0];
            
        } else {
            /* No touch */
            if (ctx->was_pressed) {
                ctx->was_pressed = false;
            }
            
            /* Update LVGL data structure */
            data->state = LV_INDEV_STATE_RELEASED;
            data->point.x = ctx->last_x;
            data->point.y = ctx->last_y;
        }
    } else {
        /* Read error - report as released */
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = ctx->last_x;
        data->point.y = ctx->last_y;
    }
}

/**
 * @brief Get touch statistics (safe to call from main loop)
 */
void lvgl_get_touch_stats(uint32_t *poll_count, uint32_t *touch_count)
{
    if (poll_count) *poll_count = g_touch_ctx.poll_count;
    if (touch_count) *touch_count = g_touch_ctx.touch_count;
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
    
    /* Initialize global touch context */
    g_touch_ctx.touch_handle = touch_handle;
    g_touch_ctx.was_pressed = false;
    g_touch_ctx.last_x = 0;
    g_touch_ctx.last_y = 0;
    g_touch_ctx.poll_count = 0;
    g_touch_ctx.touch_count = 0;
    
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
            .swap_xy = false,  // Landscape mode (no coordinate swap)
            .mirror_x = false, 
            .mirror_y = false 
        },
        .flags = {
            .buff_dma = true,       // For DSI, always uses frame buffer from driver
            .buff_spiram = true,
            .sw_rotate = true,      // Keep enabled like vendor demo
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
    ESP_LOGI(TAG, "DSI flush callback registered (will log every 100 flushes)");
    
    /* Create custom touch input device with manual read callback */
    ESP_LOGI(TAG, "Creating custom touch input device...");
    
    g_touch_indev = lv_indev_create();
    if (!g_touch_indev) {
        ESP_LOGE(TAG, "❌ Failed to create LVGL indev!");
        return ESP_FAIL;
    }
    
    lv_indev_set_type(g_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_touch_indev, touch_read_cb);
    lv_indev_set_display(g_touch_indev, disp);
    
    ESP_LOGW(TAG, "⚠️  CUSTOM TOUCH INPUT DEVICE REGISTERED");
    ESP_LOGI(TAG, "    Using direct GT911 polling (NO logging in callback)");
    ESP_LOGI(TAG, "    Visual feedback: FPS counter + GT911 periodic summary");
    
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
    ESP_LOGI(TAG, "  Touch: Custom indev (silent polling, visual feedback)");
    ESP_LOGI(TAG, "  Rotation: swap_xy=false (landscape mode)");
    ESP_LOGI(TAG, "  Flush callback: ACTIVE (monitoring enabled)");
    ESP_LOGI(TAG, "========================================");
    
    return ESP_OK;
}
