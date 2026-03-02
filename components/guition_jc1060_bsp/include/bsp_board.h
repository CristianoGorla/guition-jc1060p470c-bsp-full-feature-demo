/*
 * Guition JC1060P470C Board Support Package
 * 
 * Public BSP interface for hardware initialization and configuration.
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Guition JC1060P470C board hardware
 * 
 * This function initializes all enabled hardware components based on
 * the Kconfig configuration (CONFIG_BSP_ENABLE_*).
 * 
 * @return
 *     - ESP_OK on success
 *     - ESP_FAIL on failure
 */
esp_err_t bsp_board_init(void);

#ifdef __cplusplus
}
#endif
