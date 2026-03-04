/*
 * SPDX-FileCopyrightText: 2024 Cristiano Gorla
 * SPDX-License-Identifier: Apache-2.0
 * 
 * Guition JC1060P470C - LVGL Demo Applications Header
 */

#ifndef LVGL_DEMO_H
#define LVGL_DEMO_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LVGL demo types
 */
typedef enum {
    LVGL_DEMO_NONE = 0,
    LVGL_DEMO_SIMPLE,
    LVGL_DEMO_WIDGETS,
    LVGL_DEMO_BENCHMARK,
    LVGL_DEMO_STRESS
} lvgl_demo_type_t;

/**
 * @brief Run a simple LVGL demo
 * @return ESP_OK on success
 */
esp_err_t lvgl_demo_simple(void);

/**
 * @brief Run LVGL widgets demo
 * @return ESP_OK on success
 */
esp_err_t lvgl_demo_widgets(void);

/**
 * @brief Run LVGL benchmark demo
 * @return ESP_OK on success
 */
esp_err_t lvgl_demo_benchmark(void);

/**
 * @brief Run LVGL stress test demo
 * @return ESP_OK on success
 */
esp_err_t lvgl_demo_stress(void);

/**
 * @brief Stop current demo
 * @return ESP_OK on success
 */
esp_err_t lvgl_demo_stop(void);

/**
 * @brief Run a specific demo type
 * @param demo Demo type to run
 * @return ESP_OK on success
 */
esp_err_t lvgl_demo_run(lvgl_demo_type_t demo);

/**
 * @brief Run demo based on Kconfig settings
 * @return ESP_OK on success
 */
esp_err_t lvgl_demo_run_from_config(void);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_DEMO_H */
