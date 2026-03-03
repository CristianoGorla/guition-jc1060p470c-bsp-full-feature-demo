/*
 * Backlight PWM Test Utility
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "backlight_test.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "BACKLIGHT_TEST";

#ifndef CONFIG_BSP_PIN_LCD_BL
#define CONFIG_BSP_PIN_LCD_BL 23
#endif

#define LEDC_TIMER              LEDC_TIMER_1
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT
#define LEDC_FREQUENCY          (20000) // 20kHz
#define LEDC_MAX_DUTY           (1023)  // 10-bit resolution

static void backlight_set_duty(uint32_t duty_cycle)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty_cycle));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
}

static void backlight_set_brightness_percent(int percent)
{
    if (percent > 100) percent = 100;
    if (percent < 0) percent = 0;
    
    uint32_t duty = (LEDC_MAX_DUTY * percent) / 100;
    ESP_LOGI(TAG, "Brightness: %3d%% (duty: %4lu/1023)", percent, duty);
    backlight_set_duty(duty);
}

void backlight_test_run(void)
{
    ESP_LOGI(TAG, "Initializing backlight PWM on GPIO %d...", CONFIG_BSP_PIN_LCD_BL);
    
    // Configure LEDC timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    
    // Configure LEDC channel
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = CONFIG_BSP_PIN_LCD_BL,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    
    ESP_LOGI(TAG, "Starting ramp test...");
    
    // Phase 1: Fade in (0% -> 100%)
    ESP_LOGI(TAG, "Phase 1: Fade IN (0%% -> 100%%)");
    for (int i = 0; i <= 100; i += 10) {
        backlight_set_brightness_percent(i);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    // Phase 2: Hold at 100%
    ESP_LOGI(TAG, "Phase 2: Hold at 100%%");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Phase 3: Fade out (100% -> 0%)
    ESP_LOGI(TAG, "Phase 3: Fade OUT (100%% -> 0%%)");
    for (int i = 100; i >= 0; i -= 10) {
        backlight_set_brightness_percent(i);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    // Phase 4: Restore to full brightness
    ESP_LOGI(TAG, "Phase 4: Restore to 100%%");
    vTaskDelay(pdMS_TO_TICKS(500));
    backlight_set_brightness_percent(100);
    
    ESP_LOGI(TAG, "Backlight test complete!");
}
