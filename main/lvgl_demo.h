/*
 * SPDX-FileCopyrightText: 2024 Cristiano Gorla
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Guition JC1060P470C - LVGL Demo Applications
 */

#pragma once

#include "sdkconfig.h"

#ifdef CONFIG_BSP_ENABLE_LVGL

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LVGL demo types matching Kconfig choices
 */
typedef enum {
    LVGL_DEMO_NONE = 0,      /*!< No demo */
    LVGL_DEMO_SIMPLE,        /*!< Simple test screen */
    LVGL_DEMO_WIDGETS,       /*!< LVGL widgets showcase */
    LVGL_DEMO_BENCHMARK,     /*!< Performance benchmark */
    LVGL_DEMO_STRESS,        /*!< Stress test */
} lvgl_demo_type_t;

/**
 * @brief Run LVGL demo based on Kconfig selection
 * 
 * Automatically runs the demo selected in menuconfig:
 * - CONFIG_BSP_LVGL_DEMO_SIMPLE
 * - CONFIG_BSP_LVGL_DEMO_WIDGETS
 * - CONFIG_BSP_LVGL_DEMO_BENCHMARK
 * - CONFIG_BSP_LVGL_DEMO_STRESS
 * 
 * @return 
 *      - ESP_OK on success
 *      - ESP_FAIL if demo initialization failed
 *      - ESP_ERR_NOT_SUPPORTED if demo type not supported
 * 
 * @note Requires CONFIG_BSP_LVGL_ENABLE_DEMO to be enabled
 *       Call after bsp_lvgl_init()
 */
esp_err_t lvgl_demo_run_from_config(void);

/**
 * @brief Run specific LVGL demo
 * 
 * @param[in] demo Demo type to run
 * @return 
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if demo type is invalid
 *      - ESP_FAIL if demo initialization failed
 */
esp_err_t lvgl_demo_run(lvgl_demo_type_t demo);

/**
 * @brief Run simple test screen demo
 * 
 * Creates a basic UI with:
 * - Welcome message
 * - Display resolution info
 * - Touch test area
 * - Color gradient
 * - FPS counter
 * 
 * Minimal memory usage, good for initial testing.
 * 
 * @return 
 *      - ESP_OK on success
 *      - ESP_FAIL on failure
 */
esp_err_t lvgl_demo_simple(void);

/**
 * @brief Run LVGL widgets demo
 * 
 * Comprehensive showcase of LVGL widgets:
 * - Buttons, sliders, switches
 * - Charts, meters, gauges
 * - Text areas, keyboards
 * - Lists, dropdowns, tables
 * - And more...
 * 
 * Requires more memory (~200KB+).
 * 
 * @return 
 *      - ESP_OK on success
 *      - ESP_FAIL on failure
 * 
 * @note Requires LVGL demos to be included in components
 */
esp_err_t lvgl_demo_widgets(void);

/**
 * @brief Run LVGL benchmark demo
 * 
 * Performance test measuring:
 * - Rendering speed (FPS)
 * - Fill performance
 * - Image rendering
 * - Text rendering
 * - Geometric shapes
 * 
 * Displays results on screen with frame time graphs.
 * 
 * @return 
 *      - ESP_OK on success
 *      - ESP_FAIL on failure
 * 
 * @note Requires LVGL demos to be included in components
 */
esp_err_t lvgl_demo_benchmark(void);

/**
 * @brief Run LVGL stress test
 * 
 * Heavy stress test with:
 * - Many animated objects
 * - Rapid screen updates
 * - Memory allocation stress
 * - Touch input handling
 * 
 * Tests system stability under high graphics load.
 * 
 * @return 
 *      - ESP_OK on success
 *      - ESP_FAIL on failure
 * 
 * @note Requires LVGL demos to be included in components
 */
esp_err_t lvgl_demo_stress(void);

/**
 * @brief Stop running demo and clear screen
 * 
 * @return ESP_OK
 */
esp_err_t lvgl_demo_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_BSP_ENABLE_LVGL */
