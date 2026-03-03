/*
 * LVGL Initialization Header
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#ifndef LVGL_INIT_H
#define LVGL_INIT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LVGL port with custom configuration
 * 
 * Must be called after bsp_board_init().
 * Retrieves display and touch handles from BSP and
 * initializes LVGL graphics stack.
 * 
 * @return
 *     - ESP_OK: Success
 *     - ESP_FAIL: Display or touch not initialized
 */
esp_err_t lvgl_port_init_custom(void);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_INIT_H */
