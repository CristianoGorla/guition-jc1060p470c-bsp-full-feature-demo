/**
 * @file lvgl_demo.h
 * @brief LVGL Demo Interface
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "feature_flags.h"

#if ENABLE_LVGL && ENABLE_LVGL_DEMO

/**
 * @brief Run LVGL demo
 * 
 * Demo type is controlled by LVGL_DEMO_TYPE in feature_flags.h:
 * - 0: Simple test screen (default)
 * - 1: LVGL Widgets demo
 * - 2: LVGL Benchmark
 * - 3: LVGL Stress test
 */
void lvgl_demo_run(void);

#endif // ENABLE_LVGL && ENABLE_LVGL_DEMO

#ifdef __cplusplus
}
#endif
