/*
 * Guition JC1060P470C Board Support Package
 * Hardware Abstraction Layer
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the board support package
 * 
 * Performs hardware initialization:
 * - Phase A: Power Manager (hard reset, power rails)
 * - Phase D: Peripheral Drivers (I2C, Display HW, Touch HW, Audio, RTC, Radar)
 * 
 * NOTE: Only initializes hardware drivers. Application layer (LVGL)
 *       should be initialized separately in main.
 * 
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_*: Failure
 */
esp_err_t bsp_board_init(void);

/**
 * @brief Get display panel handle
 * 
 * @return Display panel handle (NULL if not initialized)
 */
esp_lcd_panel_handle_t bsp_display_get_handle(void);

/**
 * @brief Get touch controller handle
 * 
 * @return Touch handle (NULL if not initialized)
 */
esp_lcd_touch_handle_t bsp_touch_get_handle(void);

/**
 * @brief Get I2C bus handle
 * 
 * @return I2C master bus handle (NULL if not initialized)
 */
i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void);

/**
 * @brief Deinitialize the board support package
 * 
 * Cleanup resources allocated during initialization.
 */
void bsp_board_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BOARD_H */
