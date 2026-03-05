#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run backlight sweep test with white screen
 * 
 * This test verifies display hardware is working by:
 * 1. Creating a white LVGL screen
 * 2. Sweeping backlight PWM: 100% → 0% → 100% → 0%
 * 3. Visual confirmation that display panel works
 * 
 * If you see brightness changing = HW OK, problem is LVGL rendering
 * If screen stays blank = Check MIPI-DSI init or panel power
 */
void backlight_test_run(void);

#ifdef __cplusplus
}
#endif
