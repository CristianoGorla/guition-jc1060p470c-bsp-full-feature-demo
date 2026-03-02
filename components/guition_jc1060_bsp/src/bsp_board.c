/*
 * Guition JC1060P470C Board Support Package
 * 
 * BSP implementation for hardware initialization.
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "bsp_board.h"
#include "esp_log.h"

static const char *TAG = "BSP";

esp_err_t bsp_board_init(void)
{
    ESP_LOGI(TAG, "Guition BSP v1.1.0-dev: Initializing hardware...");
    
    // Hardware initialization will be implemented in future steps
    // This stub prepares the infrastructure for the transition
    
    return ESP_OK;
}
