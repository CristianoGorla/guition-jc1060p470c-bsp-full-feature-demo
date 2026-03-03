/**
 * @file display_hw_test.h
 * @brief Display hardware test API
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test hardware pattern generation
 * 
 * Enables MIPI DSI built-in test pattern (horizontal color bars).
 * This validates DSI PHY communication without framebuffer.
 * 
 * @param panel_handle LCD panel handle
 * @return ESP_OK on success
 */
esp_err_t display_hw_test_pattern(esp_lcd_panel_handle_t panel_handle);

/**
 * @brief Test software rendering with color bars
 * 
 * Renders 8 vertical SMPTE color bars to validate full rendering pipeline:
 * - PSRAM buffer allocation
 * - Software rendering
 * - DMA copy to display framebuffer
 * - Display refresh
 * 
 * @param panel_handle LCD panel handle
 * @param width Display width in pixels
 * @param height Display height in pixels
 * @return ESP_OK on success
 */
esp_err_t display_hw_test_color_bar(esp_lcd_panel_handle_t panel_handle, int width, int height);

#ifdef __cplusplus
}
#endif
