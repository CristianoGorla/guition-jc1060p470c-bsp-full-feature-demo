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
static uint32_t g_flush_count = 0;
static uint32_t g_manual_poll_count = 0;
static uint32_t g_touch_press_count = 0;

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
 * @brief Manual touch polling timer callback
 * 
 * Bypasses lvgl_port automatic polling which may be disabled with avoid_tearing=false.
 * Directly reads GT911 via esp_lcd_touch and injects events into LVGL indev.
 */
static void manual_touch_poll_cb(lv_timer_t *timer)
{
    typedef struct {
        esp_lcd_touch_handle_t touch_handle;
        lv_indev_t *indev;
    } poll_ctx_t;
    
    poll_ctx_t *ctx = (poll_ctx_t *)timer->user_data;
    static bool was_pressed = false;
    static uint16_t last_x = 0, last_y = 0;
    
    g_manual_poll_count++;
    
    /* Periodic heartbeat log */
    if (g_manual_poll_count % 500 == 0) {
        ESP_LOGI(TAG, "⏱️  Manual touch poll alive: %lu polls, %lu touches detected", 
                 g_manual_poll_count, g_touch_press_count);
    }
    
    uint16_t x[1], y[1], strength[1];
    uint8_t touch_cnt = 0;
    
    /* Direct read from GT911 driver */
    esp_err_t ret = esp_lcd_touch_read_data(ctx->touch_handle, x, y, strength, &touch_cnt, 1);
    
    if (ret == ESP_OK && touch_cnt > 0) {
        /* Touch detected */
        if (!was_pressed) {
            g_touch_press_count++;
            ESP_LOGI(TAG, "🟢 Touch PRESSED at (%d, %d) [strength=%d]", x[0], y[0], strength[0]);
            was_pressed = true;
        }
        
        /* Update LVGL indev state */
        lv_indev_data_t data = {
            .state = LV_INDEV_STATE_PRESSED,
            .point = {.x = x[0], .y = y[0]}
        };
        
        /* CRITICAL: Must lock LVGL before modifying indev state */
        lvgl_port_lock(0);
        lv_indev_set_data(ctx->indev, &data);
        lvgl_port_unlock();
        
        last_x = x[0];
        last_y = y[0];
        
    } else {
        /* No touch or read error */
        if (was_pressed) {
            ESP_LOGI(TAG, "🔴 Touch RELEASED at (%d, %d)", last_x, last_y);
            was_pressed = false;
        }
        
        /* Update LVGL indev state */
        lv_indev_data_t data = {
            .state = LV_INDEV_STATE_RELEASED,
            .point = {.x = last_x, .y = last_y}
        };
        
        /* CRITICAL: Must lock LVGL before modifying indev state */
        lvgl_port_lock(0);
        lv_indev_set_data(ctx->indev, &data);
        lvgl_port_unlock();
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
    
    /* Add touch device to LVGL (may not enable automatic polling with avoid_tearing=false) */
    ESP_LOGI(TAG, "Adding touch device...");
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
    ESP_LOGI(TAG, "✅ Touch device added, type: %d (expected 1=POINTER)", indev_type);
    
    /* FIX: Implement manual touch polling to bypass lvgl_port automatic polling bug */
    /* Allocate context on heap (timer outlives this function) */
    typedef struct {
        esp_lcd_touch_handle_t touch_handle;
        lv_indev_t *indev;
    } poll_ctx_t;
    
    poll_ctx_t *poll_ctx = heap_caps_malloc(sizeof(poll_ctx_t), MALLOC_CAP_DEFAULT);
    if (!poll_ctx) {
        ESP_LOGE(TAG, "Failed to allocate poll context!");
        return ESP_FAIL;
    }
    
    poll_ctx->touch_handle = touch_handle;
    poll_ctx->indev = g_touch_indev;
    
    /* Create manual polling timer - 50Hz (20ms) to match GT911 sample rate */
    lv_timer_t *poll_timer = lv_timer_create(manual_touch_poll_cb, 20, poll_ctx);
    if (!poll_timer) {
        ESP_LOGE(TAG, "Failed to create touch poll timer!");
        free(poll_ctx);
        return ESP_FAIL;
    }
    
    ESP_LOGW(TAG, "⚠️  MANUAL TOUCH POLLING ENABLED (50Hz)");
    ESP_LOGI(TAG, "    This bypasses lvgl_port automatic polling");
    ESP_LOGI(TAG, "    Touch events will be logged with 🟢/🔴 markers");
    
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
    ESP_LOGI(TAG, "  Touch: Manual polling at 50Hz (20ms period)");
    ESP_LOGI(TAG, "  Rotation: swap_xy=false (landscape mode)");
    ESP_LOGI(TAG, "  Flush callback: ACTIVE (monitoring enabled)");
    ESP_LOGI(TAG, "========================================");
    
    return ESP_OK;
}
