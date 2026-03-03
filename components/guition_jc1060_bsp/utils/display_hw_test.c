/**
 * @file display_hw_test.c
 * @brief Hardware display test implementation for JC1060 BSP
 */

#include "display_hw_test.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_lcd_mipi_dsi.h"

static const char *TAG = "display_hw_test";

// Semaphore for refresh synchronization
static SemaphoreHandle_t refresh_finish = NULL;

/**
 * @brief DPI refresh done callback
 */
static bool on_color_trans_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    BaseType_t need_yield = pdFALSE;
    xSemaphoreGiveFromISR(refresh_finish, &need_yield);
    return (need_yield == pdTRUE);
}

esp_err_t display_hw_test_pattern(esp_lcd_panel_handle_t panel_handle)
{
    if (!panel_handle) {
        ESP_LOGE(TAG, "Invalid panel handle");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Testing hardware pattern (horizontal color bar)");
    
    // Set hardware-generated horizontal color bar pattern
    esp_err_t ret = esp_lcd_dpi_panel_set_pattern(panel_handle, MIPI_DSI_PATTERN_BAR_HORIZONTAL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set hardware pattern: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Hardware pattern test started successfully");
    ESP_LOGI(TAG, "You should see horizontal color bars on the display");
    
    return ESP_OK;
}

esp_err_t display_hw_test_color_bar(esp_lcd_panel_handle_t panel_handle, uint16_t h_res, uint16_t v_res)
{
    if (!panel_handle) {
        ESP_LOGE(TAG, "Invalid panel handle");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Testing software rendering (%dx%d)", h_res, v_res);

    // Create refresh finish semaphore if not exists
    if (!refresh_finish) {
        refresh_finish = xSemaphoreCreateBinary();
        if (!refresh_finish) {
            ESP_LOGE(TAG, "Failed to create refresh semaphore");
            return ESP_ERR_NO_MEM;
        }

        // Register callback for refresh done event
        esp_lcd_dpi_panel_event_callbacks_t cbs = {
            .on_color_trans_done = on_color_trans_done,
        };
        esp_err_t ret = esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &cbs, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register callback: %s", esp_err_to_name(ret));
            vSemaphoreDelete(refresh_finish);
            refresh_finish = NULL;
            return ret;
        }
    }

    // Calculate buffer size (RGB888 = 3 bytes per pixel)
    const uint8_t byte_per_pixel = 3;
    uint32_t buffer_size = h_res * v_res * byte_per_pixel;
    
    ESP_LOGI(TAG, "Allocating %lu bytes for framebuffer in PSRAM", buffer_size);
    
    // Allocate frame buffer in PSRAM with DMA capability
    uint8_t *frame_buffer = (uint8_t *)heap_caps_calloc(1, buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
    if (!frame_buffer) {
        ESP_LOGE(TAG, "Failed to allocate frame buffer");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Drawing color bars...");
    
    // Draw vertical color bars
    // Divide screen into 8 color bars: White, Yellow, Cyan, Green, Magenta, Red, Blue, Black
    uint8_t colors[8][3] = {
        {255, 255, 255}, // White
        {255, 255, 0},   // Yellow  
        {0, 255, 255},   // Cyan
        {0, 255, 0},     // Green
        {255, 0, 255},   // Magenta
        {255, 0, 0},     // Red
        {0, 0, 255},     // Blue
        {0, 0, 0}        // Black
    };
    
    uint16_t bar_width = h_res / 8;
    
    for (uint16_t y = 0; y < v_res; y++) {
        for (uint16_t x = 0; x < h_res; x++) {
            uint8_t bar_index = x / bar_width;
            if (bar_index >= 8) bar_index = 7;
            
            uint32_t pixel_index = (y * h_res + x) * byte_per_pixel;
            frame_buffer[pixel_index + 0] = colors[bar_index][0]; // R
            frame_buffer[pixel_index + 1] = colors[bar_index][1]; // G  
            frame_buffer[pixel_index + 2] = colors[bar_index][2]; // B
        }
    }

    ESP_LOGI(TAG, "Drawing bitmap to display...");
    
    // Draw the bitmap
    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, h_res, v_res, frame_buffer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to draw bitmap: %s", esp_err_to_name(ret));
        heap_caps_free(frame_buffer);
        return ret;
    }

    // Wait for refresh to complete
    ESP_LOGI(TAG, "Waiting for display refresh...");
    xSemaphoreTake(refresh_finish, portMAX_DELAY);
    
    ESP_LOGI(TAG, "Software color bar test completed successfully");
    ESP_LOGI(TAG, "You should see 8 vertical color bars on the display");
    
    // Free the frame buffer
    heap_caps_free(frame_buffer);
    
    return ESP_OK;
}
