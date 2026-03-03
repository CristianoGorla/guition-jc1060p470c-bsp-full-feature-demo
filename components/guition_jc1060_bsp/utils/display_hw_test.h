/**
 * @file display_hw_test.h
 * @brief Hardware display test utilities for JC1060 BSP
 * 
 * Test functions for MIPI DSI display hardware without LVGL
 */

#ifndef DISPLAY_HW_TEST_H
#define DISPLAY_HW_TEST_H

#include "esp_err.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run hardware pattern test on display
 * 
 * Tests the display using hardware-generated color bar pattern.
 * This test doesn't require DMA buffers or software rendering.
 * 
 * @param panel_handle LCD panel handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_hw_test_pattern(esp_lcd_panel_handle_t panel_handle);

/**
 * @brief Run software rendering color bar test
 * 
 * Tests the display by rendering a color bar pattern in software
 * and drawing it using DMA.
 * 
 * @param panel_handle LCD panel handle
 * @param h_res Horizontal resolution
 * @param v_res Vertical resolution
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_hw_test_color_bar(esp_lcd_panel_handle_t panel_handle, uint16_t h_res, uint16_t v_res);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_HW_TEST_H
