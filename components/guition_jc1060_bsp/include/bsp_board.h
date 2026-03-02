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
 * Performs Phase A (Power Manager) initialization:
 * - Deterministic hard reset sequence
 * - GPIO 18 strapping guard for ESP32-C6 boot mode
 * - Power rail stabilization
 * 
 * @return
 *     - ESP_OK: Success
 *     - ESP_ERR_*: Failure
 */
esp_err_t bsp_board_init(void);

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
