/**
 * @file bsp_lvgl.h
 * @brief LVGL Integration for Guition JC1060P470C BSP
 * 
 * This header provides the public API for LVGL integration using esp_lvgl_port.
 * Supports LVGL 9.2.2 with MIPI DSI display and GT911 touch controller.
 * 
 * Features:
 * - Hardware-accelerated display via MIPI DSI
 * - Multi-touch support (GT911)
 * - Thread-safe LVGL operations (lock/unlock)
 * - Configurable rotation (0°, 90°, 180°, 270°)
 * - Double buffering support
 * - DMA and SPIRAM buffer options
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

/**
 * @brief LVGL display configuration
 */
typedef struct {
    uint32_t buffer_size;        /*!< LVGL draw buffer size in pixels */
    bool double_buffer;          /*!< Enable double buffering */
    bool buff_dma;               /*!< Allocate buffers in DMA-capable memory */
    bool buff_spiram;            /*!< Allocate buffers in SPIRAM */
    bool sw_rotate;              /*!< Enable software rotation */
    lv_disp_rotation_t rotation; /*!< Initial display rotation */
} bsp_lvgl_config_t;

/**
 * @brief Default LVGL configuration
 * 
 * Buffer size: 100 lines (1024 * 100 pixels)
 * Double buffer: Enabled for smooth rendering
 * DMA buffers: Enabled for RGB565 (required for DSI)
 * Software rotation: Enabled (hardware rotation not supported on DSI)
 */
#define BSP_LVGL_CONFIG_DEFAULT() {                    \
    .buffer_size = 1024 * 100,                         \
    .double_buffer = true,                             \
    .buff_dma = true,                                  \
    .buff_spiram = false,                              \
    .sw_rotate = true,                                 \
    .rotation = LV_DISPLAY_ROTATION_0,                 \
}

/**
 * @brief Initialize and start LVGL with default configuration
 * 
 * This function:
 * 1. Initializes LVGL port (lvgl_port_init)
 * 2. Creates display with JD9165 DSI panel
 * 3. Registers GT911 touch input
 * 4. Sets display brightness to 100%
 * 5. Starts LVGL task
 * 
 * @return LVGL display handle on success, NULL on failure
 * 
 * @note After this call, use bsp_lvgl_lock() before any LVGL API calls
 */
lv_display_t *bsp_lvgl_start(void);

/**
 * @brief Initialize and start LVGL with custom configuration
 * 
 * @param config Pointer to LVGL configuration structure
 * @return LVGL display handle on success, NULL on failure
 */
lv_display_t *bsp_lvgl_start_with_config(const bsp_lvgl_config_t *config);

/**
 * @brief Get LVGL touch input device handle
 * 
 * @return Touch input device handle, NULL if not initialized
 */
lv_indev_t *bsp_lvgl_get_touch_input(void);

/**
 * @brief Lock LVGL for thread-safe operations
 * 
 * Call this before any LVGL API calls from non-LVGL tasks.
 * Must be followed by bsp_lvgl_unlock().
 * 
 * Example:
 * @code
 * if (bsp_lvgl_lock(1000)) {
 *     lv_label_set_text(label, "Hello");
 *     bsp_lvgl_unlock();
 * }
 * @endcode
 * 
 * @param timeout_ms Timeout in milliseconds (0 = no wait, -1 = wait forever)
 * @return true if lock acquired, false on timeout
 */
bool bsp_lvgl_lock(uint32_t timeout_ms);

/**
 * @brief Unlock LVGL after thread-safe operations
 * 
 * Must be called after bsp_lvgl_lock() to release the mutex.
 */
void bsp_lvgl_unlock(void);

/**
 * @brief Change display rotation
 * 
 * @param disp Display handle (from bsp_lvgl_start)
 * @param rotation Rotation angle (LV_DISPLAY_ROTATION_0/90/180/270)
 * 
 * @note Must be called between bsp_lvgl_lock() and bsp_lvgl_unlock()
 */
void bsp_lvgl_rotate(lv_display_t *disp, lv_disp_rotation_t rotation);

#ifdef __cplusplus
}
#endif
