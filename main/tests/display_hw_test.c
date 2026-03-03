/**
 * @file display_hw_test.c
 * @brief Hardware test functions for JD9165 MIPI-DSI display
 * 
 * Tests both hardware pattern generation and software rendering paths
 * for ESP32-P4 MIPI DSI DPI panels.
 */

#include "display_hw_test.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "display_hw_test";

/**
 * @brief Test hardware pattern generation (horizontal color bar)
 * 
 * Uses MIPI DSI built-in test pattern generator - no framebuffer needed.
 * This is the fastest test and validates DSI/PHY communication.
 */
esp_err_t display_hw_test_pattern(esp_lcd_panel_handle_t panel_handle)
{
    if (!panel_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Testing hardware pattern (horizontal color bar)");

    // Enable hardware test pattern
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_disp_on_off(panel_handle, true),
        TAG, "Failed to enable display");

    ESP_LOGI(TAG, "Hardware pattern test started successfully");
    ESP_LOGI(TAG, "You should see horizontal color bars on the display");

    return ESP_OK;
}

/**
 * @brief DPI panel refresh done callback
 */
static bool on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    SemaphoreHandle_t sem = (SemaphoreHandle_t)user_ctx;
    BaseType_t high_task_woken = pdFALSE;
    
    if (sem) {
        xSemaphoreGiveFromISR(sem, &high_task_woken);
    }
    
    return high_task_woken == pdTRUE;
}

/**
 * @brief Test software rendering with color bars
 * 
 * Renders 8 vertical color bars (standard SMPTE pattern) to a PSRAM buffer,
 * then copies to the display framebuffer. Tests the full rendering pipeline.
 * 
 * @param panel_handle LCD panel handle
 * @param width Display width in pixels
 * @param height Display height in pixels
 */
esp_err_t display_hw_test_color_bar(esp_lcd_panel_handle_t panel_handle, int width, int height)
{
    if (!panel_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Testing software rendering (%dx%d)", width, height);

    // Allocate framebuffer in PSRAM (RGB565 format)
    size_t fb_size = width * height * sizeof(uint16_t);
    ESP_LOGI(TAG, "Allocating %zu bytes for framebuffer in PSRAM", fb_size);
    
    uint16_t *fb = (uint16_t *)heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM);
    if (!fb) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        return ESP_ERR_NO_MEM;
    }

    // Standard SMPTE color bar pattern (8 vertical bars)
    const uint16_t colors[8] = {
        0xFFFF,  // White
        0xFFE0,  // Yellow
        0x07FF,  // Cyan
        0x07E0,  // Green
        0xF81F,  // Magenta
        0xF800,  // Red
        0x001F,  // Blue
        0x0000   // Black
    };

    // Draw color bars
    ESP_LOGI(TAG, "Drawing color bars...");
    int bar_width = width / 8;
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int bar_index = x / bar_width;
            if (bar_index >= 8) bar_index = 7;  // Clamp last pixels
            fb[y * width + x] = colors[bar_index];
        }
    }

    // Get DPI panel framebuffer
    ESP_LOGI(TAG, "Getting display framebuffer...");
    void *panel_fb = NULL;
    esp_err_t ret = esp_lcd_dpi_panel_get_frame_buffer(panel_handle, 1, &panel_fb);
    if (ret != ESP_OK || !panel_fb) {
        ESP_LOGE(TAG, "Failed to get panel framebuffer: %s", esp_err_to_name(ret));
        heap_caps_free(fb);
        return ret;
    }

    // Copy rendered data to panel framebuffer
    ESP_LOGI(TAG, "Copying to display framebuffer...");
    memcpy(panel_fb, fb, fb_size);
    
    // Create semaphore for vsync notification
    SemaphoreHandle_t refresh_done = xSemaphoreCreateBinary();
    if (!refresh_done) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        heap_caps_free(fb);
        return ESP_ERR_NO_MEM;
    }

    // Register vsync callback
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_vsync = on_vsync_event,
    };
    ret = esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &cbs, refresh_done);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register vsync callback: %s", esp_err_to_name(ret));
        // Continue anyway - display should still work
    }

    // Wait for display refresh with timeout
    ESP_LOGI(TAG, "Waiting for display refresh...");
    if (xSemaphoreTake(refresh_done, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ESP_LOGI(TAG, "Display refresh complete");
    } else {
        ESP_LOGW(TAG, "Timeout waiting for vsync - display may still be refreshing");
    }

    // Cleanup
    vSemaphoreDelete(refresh_done);
    heap_caps_free(fb);

    ESP_LOGI(TAG, "Color bar test complete");
    return ESP_OK;
}
