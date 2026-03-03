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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the board support package
 * 
 * Performs hardware initialization:
 * - Phase A: Power Manager (hard reset, power rails)
 * - Phase D: Peripheral Drivers (I2C, Display HW, Touch HW, Audio, RTC)
 * 
 * NOTE: LVGL is NOT initialized by this function!
 *       Call bsp_lvgl_init() separately AFTER bootstrap completes.
 * 
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_*: Failure
 */
esp_err_t bsp_board_init(void);

/**
 * @brief Initialize LVGL graphics library
 * 
 * Must be called AFTER:
 * 1. bsp_board_init() (hardware ready)
 * 2. Bootstrap manager completion (WiFi/SD initialized)
 * 
 * This separation prevents LVGL task from blocking during
 * SDMMC controller initialization for WiFi/SD.
 * 
 * @return
 *     - ESP_OK: Success
 *     - ESP_FAIL: Display or touch not initialized
 */
esp_err_t bsp_lvgl_init(void);

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
