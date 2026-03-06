/*
 * OV02C10 Camera Wrapper (Phase 1: Detection)
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
 * @brief Power-on/reset sequence for OV02C10 XSHUTDN pin.
 *
 * Sequence:
 * 1) XSHUTDN LOW
 * 2) Delay 5ms
 * 3) XSHUTDN HIGH
 */
esp_err_t bsp_camera_power_on(void);

/**
 * @brief Initialize OV02C10 SCCB interface and detect chip ID.
 *
 * Reads CHIP_ID bytes from 0x300A/0x300B/0x300C and verifies
 * expected ID (0x560243).
 */
esp_err_t bsp_camera_init(void);

/**
 * @brief Deinitialize camera wrapper resources.
 */
void bsp_camera_deinit(void);

#ifdef __cplusplus
}
#endif
