/*
 * SPDX-FileCopyrightText: 2024 Cristiano Gorla
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Guition JC1060P470C - LVGL 9.2.2 BSP Integration
 */

#pragma once

#include "sdkconfig.h"

#ifdef CONFIG_BSP_ENABLE_LVGL

#include "esp_err.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LVGL rotation values matching Kconfig choices
 */
typedef enum {
    BSP_LVGL_ROTATION_0   = LV_DISPLAY_ROTATION_0,    /*!< 0 degrees - Landscape */
    BSP_LVGL_ROTATION_90  = LV_DISPLAY_ROTATION_90,   /*!< 90 degrees - Portrait */
    BSP_LVGL_ROTATION_180 = LV_DISPLAY_ROTATION_180,  /*!< 180 degrees - Landscape inverted */
    BSP_LVGL_ROTATION_270 = LV_DISPLAY_ROTATION_270   /*!< 270 degrees - Portrait inverted */
} bsp_lvgl_rotation_t;

/**
 * @brief LVGL configuration structure (populated from Kconfig)
 */
typedef struct {
    struct {
        int buffer_lines;           /*!< Number of lines per buffer (from CONFIG_BSP_LVGL_BUFFER_LINES) */
        bool double_buffer;         /*!< Enable double buffering (from CONFIG_BSP_LVGL_DOUBLE_BUFFER) */
        bool use_dma;               /*!< Use DMA-capable memory (from CONFIG_BSP_LVGL_USE_DMA_BUFFER) */
        bool use_spiram;            /*!< Use SPIRAM for buffers (from CONFIG_BSP_LVGL_USE_SPIRAM_BUFFER) */
    } buffer;
    
    struct {
#ifdef CONFIG_BSP_LVGL_ENABLE_SW_ROTATE
        bool sw_rotate;             /*!< Enable software rotation (from CONFIG_BSP_LVGL_ENABLE_SW_ROTATE) */
        int initial_rotation;       /*!< Initial rotation in degrees (from CONFIG_BSP_LVGL_ROTATION_DEGREE) */
#endif
    } rotation;
    
    struct {
#ifdef CONFIG_BSP_LVGL_TOUCH_ENABLE
        bool enable;                /*!< Enable touch integration (from CONFIG_BSP_LVGL_TOUCH_ENABLE) */
#endif
    } touch;
    
    lvgl_port_cfg_t lvgl_port_cfg; /*!< LVGL port configuration */
} bsp_lvgl_config_t;

/**
 * @brief Default LVGL configuration macro (reads all values from Kconfig)
 * 
 * This macro creates a bsp_lvgl_config_t structure with all values
 * automatically populated from menuconfig settings.
 * 
 * Usage:
 * @code
 * bsp_lvgl_config_t config = BSP_LVGL_CONFIG_DEFAULT();
 * lv_display_t *display = bsp_lvgl_init(&config);
 * @endcode
 */
#define BSP_LVGL_CONFIG_DEFAULT() {                                      \
    .buffer = {                                                          \
        .buffer_lines = CONFIG_BSP_LVGL_BUFFER_LINES,                    \
        .double_buffer = CONFIG_BSP_LVGL_DOUBLE_BUFFER,                  \
        .use_dma = CONFIG_BSP_LVGL_USE_DMA_BUFFER,                       \
        .use_spiram = CONFIG_BSP_LVGL_USE_SPIRAM_BUFFER,                 \
    },                                                                   \
    .rotation = {                                                        \
        .sw_rotate = CONFIG_BSP_LVGL_ENABLE_SW_ROTATE,                   \
        .initial_rotation = CONFIG_BSP_LVGL_ROTATION_DEGREE,             \
    },                                                                   \
    .touch = {                                                           \
        .enable = CONFIG_BSP_LVGL_TOUCH_ENABLE,                          \
    },                                                                   \
    .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),                        \
}

/**
 * @brief Initialize LVGL with BSP configuration
 * 
 * Initializes LVGL graphics library with display and optional touch support.
 * All settings are read from Kconfig (idf.py menuconfig).
 * 
 * @param[in] config LVGL configuration (use BSP_LVGL_CONFIG_DEFAULT() for Kconfig values)
 * @return 
 *      - lv_display_t* pointer on success
 *      - NULL on failure
 * 
 * @note This function:
 *       - Initializes LVGL port
 *       - Creates display with JD9165 MIPI-DSI
 *       - Allocates draw buffers (single or double)
 *       - Optionally initializes GT911 touch
 *       - Sets initial rotation from Kconfig
 */
lv_display_t *bsp_lvgl_init(const bsp_lvgl_config_t *config);

/**
 * @brief Initialize LVGL with default configuration from Kconfig
 * 
 * Convenience function that uses BSP_LVGL_CONFIG_DEFAULT().
 * Equivalent to: bsp_lvgl_init(&BSP_LVGL_CONFIG_DEFAULT())
 * 
 * @return 
 *      - lv_display_t* pointer on success
 *      - NULL on failure
 */
lv_display_t *bsp_lvgl_init_default(void);

/**
 * @brief Deinitialize LVGL and free resources
 * 
 * Stops LVGL port, removes display, and frees all allocated memory.
 * 
 * @return 
 *      - ESP_OK on success
 *      - ESP_FAIL if LVGL was not initialized
 */
esp_err_t bsp_lvgl_deinit(void);

/**
 * @brief Get LVGL display object
 * 
 * @return 
 *      - lv_display_t* pointer if LVGL is initialized
 *      - NULL if not initialized
 */
lv_display_t *bsp_lvgl_get_display(void);

/**
 * @brief Get LVGL touch input device
 * 
 * @return 
 *      - lv_indev_t* pointer if touch is enabled and initialized
 *      - NULL if touch is disabled or not initialized
 */
lv_indev_t *bsp_lvgl_get_touch_input(void);

/**
 * @brief Set display rotation
 * 
 * @param[in] rotation Rotation value (0, 90, 180, or 270 degrees)
 * @return 
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if rotation is invalid
 *      - ESP_FAIL if LVGL is not initialized or SW rotation disabled
 * 
 * @note Requires CONFIG_BSP_LVGL_ENABLE_SW_ROTATE to be enabled
 */
esp_err_t bsp_lvgl_set_rotation(bsp_lvgl_rotation_t rotation);

/**
 * @brief Get current display rotation
 * 
 * @return Current rotation in degrees (0, 90, 180, or 270)
 */
int bsp_lvgl_get_rotation(void);

/**
 * @brief Lock LVGL mutex for thread-safe operations
 * 
 * Must be called before any LVGL API calls from non-LVGL threads.
 * Always pair with bsp_lvgl_unlock().
 * 
 * @param[in] timeout_ms Timeout in milliseconds (-1 = wait forever)
 * @return 
 *      - true if lock acquired
 *      - false if timeout
 * 
 * @note Example:
 * @code
 * if (bsp_lvgl_lock(-1)) {
 *     lv_obj_set_pos(obj, 10, 20);
 *     bsp_lvgl_unlock();
 * }
 * @endcode
 */
bool bsp_lvgl_lock(uint32_t timeout_ms);

/**
 * @brief Unlock LVGL mutex
 * 
 * Releases the lock acquired by bsp_lvgl_lock().
 */
void bsp_lvgl_unlock(void);

/**
 * @brief Get LVGL buffer size in bytes
 * 
 * Calculates total memory used for LVGL draw buffers.
 * 
 * @return Buffer size in bytes (single or double buffer depending on config)
 */
size_t bsp_lvgl_get_buffer_size(void);

/**
 * @brief Print LVGL memory and performance statistics
 * 
 * Logs current LVGL configuration and runtime statistics:
 * - Buffer configuration (size, count, location)
 * - Display resolution and rotation
 * - Touch status
 * - Memory usage
 */
void bsp_lvgl_print_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_BSP_ENABLE_LVGL */
