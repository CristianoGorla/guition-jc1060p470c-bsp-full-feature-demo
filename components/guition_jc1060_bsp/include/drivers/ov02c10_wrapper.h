/*
 * OV02C10 Camera Wrapper (Phase 2: Link + Streaming)
 *
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

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
 * @brief Start OV02C10 streaming mode (1080p profile).
 */
esp_err_t bsp_camera_start_stream(void);

/**
 * @brief Stop camera stream and put sensor in software standby.
 */
esp_err_t bsp_camera_stop_stream(void);

typedef void (*bsp_camera_preview_frame_cb_t)(void *user_data);

/**
 * @brief Start camera preview pipeline (CSI/ISP frame sync + PPA scaling).
 */
esp_err_t bsp_camera_start_preview(bsp_camera_preview_frame_cb_t frame_cb, void *user_data);

/**
 * @brief Stop camera preview task and place sensor in software standby.
 */
void bsp_camera_stop_preview(void);

/**
 * @brief Get PPA-scaled preview buffer (RGB888, 1024x600).
 */
const uint8_t *bsp_camera_get_preview_buffer(void);

size_t bsp_camera_get_preview_buffer_size(void);
uint16_t bsp_camera_get_preview_width(void);
uint16_t bsp_camera_get_preview_height(void);

/**
 * @brief Get sensor gain index supported range.
 */
esp_err_t bsp_camera_get_gain_index_range(uint32_t *min_idx, uint32_t *max_idx, uint32_t *default_idx);

/**
 * @brief Get current sensor gain index.
 */
esp_err_t bsp_camera_get_gain_index(uint32_t *gain_idx);

/**
 * @brief Set sensor gain index (value is clamped to supported range).
 */
esp_err_t bsp_camera_set_gain_index(uint32_t gain_idx);

/**
 * @brief Get exposure register supported range.
 */
esp_err_t bsp_camera_get_exposure_range(uint32_t *min_exposure, uint32_t *max_exposure, uint32_t *default_exposure);

/**
 * @brief Get current exposure register value.
 */
esp_err_t bsp_camera_get_exposure_value(uint32_t *exposure);

/**
 * @brief Set exposure register value (value is clamped to supported range).
 */
esp_err_t bsp_camera_set_exposure_value(uint32_t exposure);

/**
 * @brief Deinitialize camera wrapper resources.
 */
void bsp_camera_deinit(void);

#ifdef __cplusplus
}
#endif
