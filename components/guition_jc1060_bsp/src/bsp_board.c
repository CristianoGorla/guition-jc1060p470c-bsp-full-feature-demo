/*
 * Guition JC1060P470C Board Support Package - Implementation
 * Phase A: Power Manager + Phase D: Peripheral Drivers
 * 
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "bsp_board.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "sdkconfig.h"

/* Include driver headers */
#ifdef CONFIG_BSP_ENABLE_DISPLAY
#include "../drivers/jd9165_bsp.h"
#endif
#ifdef CONFIG_BSP_ENABLE_TOUCH
#include "../drivers/gt911_bsp.h"
#endif
#ifdef CONFIG_BSP_ENABLE_AUDIO
#include "../drivers/es8311_bsp.h"
#endif
#ifdef CONFIG_BSP_ENABLE_RTC
#include "../drivers/rx8025t_bsp.h"
#endif
#ifdef CONFIG_BSP_ENABLE_LVGL
#include "esp_lvgl_port.h"
#include "lvgl.h"
#endif

static const char *TAG = "BSP";

/* Hardware configuration from Kconfig (with fallback defaults) */
#ifndef CONFIG_BSP_PIN_SD_POWER_EN
#define CONFIG_BSP_PIN_SD_POWER_EN 36
#endif

#ifndef CONFIG_BSP_I2C_SCL_GPIO
#define CONFIG_BSP_I2C_SCL_GPIO 8
#endif

#ifndef CONFIG_BSP_I2C_SDA_GPIO
#define CONFIG_BSP_I2C_SDA_GPIO 7
#endif

#ifndef CONFIG_BSP_I2C_FREQ_HZ
#define CONFIG_BSP_I2C_FREQ_HZ 400000
#endif

#ifndef CONFIG_BSP_HARD_RESET_DISCHARGE_MS
#define CONFIG_BSP_HARD_RESET_DISCHARGE_MS 500
#endif

#ifndef CONFIG_BSP_POWER_STABILIZATION_MS
#define CONFIG_BSP_POWER_STABILIZATION_MS 100
#endif

#define SD_POWER_DELAY_MS 50

/* Global I2C bus handle (shared by all I2C peripherals) */
i2c_master_bus_handle_t g_i2c_bus_handle = NULL;

static bool bsp_needs_hard_reset(void)
{
#ifndef CONFIG_BSP_ENABLE_HARD_RESET
    return false;
#endif

    esp_reset_reason_t reason = esp_reset_reason();
    
    switch (reason) {
        case ESP_RST_POWERON:
            ESP_LOGI(TAG, "[RESET] Cold boot (power-on reset) - no hard reset needed");
            return false;
        case ESP_RST_SW:
            ESP_LOGI(TAG, "[RESET] Software reset (esp_restart) - no hard reset needed");
            return false;
        case 11:
            ESP_LOGI(TAG, "[RESET] USB-UART reset (IDF monitor) - no hard reset needed");
            return false;
        case ESP_RST_DEEPSLEEP:
            ESP_LOGI(TAG, "[RESET] Wake from deep sleep - no hard reset needed");
            return false;
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
        case ESP_RST_BROWNOUT:
            ESP_LOGW(TAG, "[RESET] Unsafe reset detected - hard reset required");
            return true;
        default:
            ESP_LOGW(TAG, "[RESET] Unknown reset reason (%d) - hard reset", reason);
            return true;
    }
}

static void bsp_hard_reset(void)
{
    if (!bsp_needs_hard_reset()) return;
    
    ESP_LOGW(TAG, "[RESET] === HARD RESET CYCLE ===");
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BSP_PIN_SD_POWER_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(CONFIG_BSP_PIN_SD_POWER_EN, 0);
    ESP_LOGI(TAG, "[RESET] Waiting %dms for discharge...", CONFIG_BSP_HARD_RESET_DISCHARGE_MS);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_BSP_HARD_RESET_DISCHARGE_MS));
    ESP_LOGI(TAG, "[RESET] Hard reset complete");
}

static esp_err_t bsp_phase_a_power_manager(void)
{
    ESP_LOGI(TAG, "[PHASE A] Power Manager starting...");
    bsp_hard_reset();
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BSP_PIN_SD_POWER_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(CONFIG_BSP_PIN_SD_POWER_EN, 0);
    ESP_LOGI(TAG, "[PHASE A] SD power OFF (GPIO%d LOW)", CONFIG_BSP_PIN_SD_POWER_EN);
    
    vTaskDelay(pdMS_TO_TICKS(CONFIG_BSP_POWER_STABILIZATION_MS));
    
    gpio_set_level(CONFIG_BSP_PIN_SD_POWER_EN, 1);
    ESP_LOGI(TAG, "[POWER] SD power ON (GPIO%d HIGH)", CONFIG_BSP_PIN_SD_POWER_EN);
    vTaskDelay(pdMS_TO_TICKS(SD_POWER_DELAY_MS));
    
    ESP_LOGI(TAG, "[PHASE A] ✓ POWER_READY");
    return ESP_OK;
}

static esp_err_t bsp_i2c_bus_init(void)
{
#if defined(CONFIG_BSP_ENABLE_TOUCH) || defined(CONFIG_BSP_ENABLE_AUDIO) || defined(CONFIG_BSP_ENABLE_RTC)
    ESP_LOGI(TAG, "[I2C] Init bus (SCL=%d, SDA=%d, %d Hz)",
             CONFIG_BSP_I2C_SCL_GPIO, CONFIG_BSP_I2C_SDA_GPIO, CONFIG_BSP_I2C_FREQ_HZ);

    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_BSP_I2C_SCL_GPIO,
        .sda_io_num = CONFIG_BSP_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &g_i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[I2C] Failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[I2C] ✓ Bus ready");
#endif
    return ESP_OK;
}

static esp_err_t bsp_phase_d_peripheral_drivers(void)
{
    ESP_LOGI(TAG, "[PHASE D] Peripheral Drivers init...");

    esp_err_t ret = bsp_i2c_bus_init();
    if (ret != ESP_OK) return ret;

#ifdef CONFIG_BSP_ENABLE_DISPLAY
    ESP_LOGI(TAG, "[PHASE D] Init display...");
    esp_lcd_panel_handle_t display = bsp_display_init();
    if (!display) {
        ESP_LOGE(TAG, "[PHASE D] Display failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[PHASE D] ✓ Display ready");
#endif

#ifdef CONFIG_BSP_ENABLE_TOUCH
    ESP_LOGI(TAG, "[PHASE D] Init touch...");
    esp_lcd_touch_handle_t touch = bsp_touch_init();
    if (!touch) {
        ESP_LOGE(TAG, "[PHASE D] Touch failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "[PHASE D] ✓ Touch ready");
#endif

#ifdef CONFIG_BSP_ENABLE_AUDIO
    ESP_LOGI(TAG, "[PHASE D] Init audio...");
    bsp_audio_config_t audio_cfg = BSP_AUDIO_DEFAULT_CONFIG();
    ret = bsp_audio_init(&audio_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[PHASE D] Audio failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[PHASE D] ✓ Audio ready");
#endif

#ifdef CONFIG_BSP_ENABLE_RTC
    ESP_LOGI(TAG, "[PHASE D] Init RTC...");
    ret = bsp_rtc_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[PHASE D] RTC failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[PHASE D] ✓ RTC ready");
#endif

#ifdef CONFIG_BSP_ENABLE_LVGL
    ESP_LOGI(TAG, "[PHASE D] Init LVGL (esp_lvgl_port)...");
    
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 6144,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));
    
    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = display,
        .buffer_size = 1024 * 50,
        .double_buffer = 1,
        .hres = 1024,
        .vres = 600,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
#ifdef CONFIG_LVGL_ENABLE_PPA
            .sw_rotate = (CONFIG_LVGL_DISP_ROTATION_DEGREES != 0),
#else
            .sw_rotate = false,
#endif
        }
    };
    
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "[PHASE D] LVGL display add failed");
        return ESP_FAIL;
    }
    
#ifdef CONFIG_LVGL_ENABLE_PPA
    if (CONFIG_LVGL_DISP_ROTATION_DEGREES == 90) {
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
    } else if (CONFIG_LVGL_DISP_ROTATION_DEGREES == 180) {
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);
    } else if (CONFIG_LVGL_DISP_ROTATION_DEGREES == 270) {
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
    }
#endif
    
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = touch,
    };
    lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg);
    if (!indev) {
        ESP_LOGW(TAG, "[PHASE D] LVGL touch add failed (non-critical)");
    }
    
    ESP_LOGI(TAG, "[PHASE D] ✓ LVGL ready (1024x600, rotation=%d°)", CONFIG_LVGL_DISP_ROTATION_DEGREES);
#endif

    ESP_LOGI(TAG, "[PHASE D] ✓ All peripherals initialized");
    return ESP_OK;
}

esp_err_t bsp_board_init(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Guition BSP v1.3.0-dev");
    ESP_LOGI(TAG, "  Phase A: Power + Phase D: Peripherals");
    ESP_LOGI(TAG, "========================================");
    
    esp_err_t ret = bsp_phase_a_power_manager();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase A failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = bsp_phase_d_peripheral_drivers();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Phase D failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  BSP Initialization Complete");
    ESP_LOGI(TAG, "========================================");
    
    return ESP_OK;
}

void bsp_board_deinit(void)
{
    if (g_i2c_bus_handle != NULL) {
        i2c_del_master_bus(g_i2c_bus_handle);
        g_i2c_bus_handle = NULL;
    }
    ESP_LOGI(TAG, "BSP deinitialized");
}
