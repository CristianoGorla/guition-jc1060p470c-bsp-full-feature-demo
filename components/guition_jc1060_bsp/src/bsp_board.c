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

i2c_master_bus_handle_t g_i2c_bus_handle = NULL;

/* Hardware handles (retained for LVGL init) */
static esp_lcd_panel_handle_t g_display_handle = NULL;
static esp_lcd_touch_handle_t g_touch_handle = NULL;

static bool bsp_needs_hard_reset(void)
{
#ifndef CONFIG_BSP_ENABLE_HARD_RESET
    return false;
#endif

    esp_reset_reason_t reason = esp_reset_reason();
    
    switch (reason) {
        case ESP_RST_POWERON:
        case ESP_RST_SW:
        case 11:
        case ESP_RST_DEEPSLEEP:
            ESP_LOGI(TAG, "[RESET] Clean boot - no hard reset");
            return false;
        default:
            ESP_LOGW(TAG, "[RESET] Unsafe reset - hard reset required");
            return true;
    }
}

static void bsp_hard_reset(void)
{
    if (!bsp_needs_hard_reset()) return;
    
    ESP_LOGW(TAG, "[RESET] === HARD RESET ===");
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BSP_PIN_SD_POWER_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(CONFIG_BSP_PIN_SD_POWER_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_BSP_HARD_RESET_DISCHARGE_MS));
}

static esp_err_t bsp_phase_a_power_manager(void)
{
    ESP_LOGI(TAG, "[PHASE A] Power Manager...");
    bsp_hard_reset();
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BSP_PIN_SD_POWER_EN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
    gpio_set_level(CONFIG_BSP_PIN_SD_POWER_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(CONFIG_BSP_POWER_STABILIZATION_MS));
    gpio_set_level(CONFIG_BSP_PIN_SD_POWER_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(SD_POWER_DELAY_MS));
    
    ESP_LOGI(TAG, "[PHASE A] ✓ POWER_READY");
    return ESP_OK;
}

static esp_err_t bsp_i2c_bus_init(void)
{
#if defined(CONFIG_BSP_ENABLE_TOUCH) || defined(CONFIG_BSP_ENABLE_AUDIO) || defined(CONFIG_BSP_ENABLE_RTC)
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_BSP_I2C_SCL_GPIO,
        .sda_io_num = CONFIG_BSP_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &g_i2c_bus_handle));
    ESP_LOGI(TAG, "[I2C] ✓ Ready");
#endif
    return ESP_OK;
}

static esp_err_t bsp_phase_d_peripheral_drivers(void)
{
    ESP_LOGI(TAG, "[PHASE D] Peripheral Drivers...");
    ESP_ERROR_CHECK(bsp_i2c_bus_init());

#ifdef CONFIG_BSP_ENABLE_DISPLAY
    g_display_handle = bsp_display_init();
    if (!g_display_handle) return ESP_FAIL;
    ESP_LOGI(TAG, "[PHASE D] ✓ Display HW");
#endif

#ifdef CONFIG_BSP_ENABLE_TOUCH
    g_touch_handle = bsp_touch_init();
    if (!g_touch_handle) return ESP_FAIL;
    ESP_LOGI(TAG, "[PHASE D] ✓ Touch HW");
#endif

#ifdef CONFIG_BSP_ENABLE_AUDIO
    bsp_audio_config_t audio_cfg = BSP_AUDIO_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(bsp_audio_init(&audio_cfg));
    ESP_LOGI(TAG, "[PHASE D] ✓ Audio");
#endif

#ifdef CONFIG_BSP_ENABLE_RTC
    ESP_ERROR_CHECK(bsp_rtc_init());
    ESP_LOGI(TAG, "[PHASE D] ✓ RTC");
#endif

    ESP_LOGI(TAG, "[PHASE D] ✓ Complete");
    return ESP_OK;
}

esp_err_t bsp_board_init(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Guition BSP v1.3.0");
    ESP_LOGI(TAG, "  Hardware Layer Only");
    ESP_LOGI(TAG, "========================================");
    
    ESP_ERROR_CHECK(bsp_phase_a_power_manager());
    ESP_ERROR_CHECK(bsp_phase_d_peripheral_drivers());
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ✓ BSP Ready (HW only)");
    ESP_LOGI(TAG, "  Call bsp_lvgl_init() after Bootstrap");
    ESP_LOGI(TAG, "========================================");
    
    return ESP_OK;
}

esp_err_t bsp_lvgl_init(void)
{
#ifdef CONFIG_BSP_ENABLE_LVGL
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  LVGL Software Layer Init");
    ESP_LOGI(TAG, "========================================");
    
    if (!g_display_handle) {
        ESP_LOGE(TAG, "Display not initialized! Call bsp_board_init() first.");
        return ESP_FAIL;
    }
    
    if (!g_touch_handle) {
        ESP_LOGE(TAG, "Touch not initialized! Call bsp_board_init() first.");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "[LVGL] Initializing port...");
    
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 6144,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));
    
    /* LVGL Display Configuration (matches vendor demo exactly) */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = NULL,  // DPI panel has no DBI I/O
        .panel_handle = g_display_handle,
        .buffer_size = 480 * 800,  /* 384,000 pixels - VENDOR CONFIG */
        .double_buffer = 1,         /* VENDOR: Double buffer enabled */
        .hres = 1024,
        .vres = 600,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,  /* Use PSRAM for LVGL buffer */
            .sw_rotate = true,    /* VENDOR: Software rotation enabled */
        }
    };
    
    /* DSI-specific configuration */
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
            .avoid_tearing = true,  /* Enable anti-tearing */
        }
    };
    
    ESP_LOGI(TAG, "[LVGL] Adding display...");
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "Failed to add LVGL display!");
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
    
    ESP_LOGI(TAG, "[LVGL] Adding touch...");
    const lvgl_port_touch_cfg_t touch_cfg = { .disp = disp, .handle = g_touch_handle };
    lvgl_port_add_touch(&touch_cfg);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ✓ LVGL Ready (1024x600, %d°)", CONFIG_LVGL_DISP_ROTATION_DEGREES);
    ESP_LOGI(TAG, "  Buffer: 480x800 (384K pixels, double)");
    ESP_LOGI(TAG, "========================================");
    
    return ESP_OK;
#else
    ESP_LOGW(TAG, "LVGL disabled in menuconfig");
    return ESP_OK;
#endif
}

i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void)
{
    return g_i2c_bus_handle;
}

void bsp_board_deinit(void)
{
    if (g_i2c_bus_handle) {
        i2c_del_master_bus(g_i2c_bus_handle);
        g_i2c_bus_handle = NULL;
    }
}
