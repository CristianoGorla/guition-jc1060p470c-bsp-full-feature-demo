/*
 * Backlight PWM Test Utility
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run backlight PWM ramp test
 * 
 * Tests the backlight by ramping brightness:
 * - 0% -> 100% (fade in)
 * - Hold at 100%
 * - 100% -> 0% (fade out)
 * - Restore to 100%
 * 
 * Uses CONFIG_BSP_PIN_LCD_BL from Kconfig.
 * Logs duty cycle and PWM values during test.
 */
void backlight_test_run(void);

#ifdef __cplusplus
}
#endif
