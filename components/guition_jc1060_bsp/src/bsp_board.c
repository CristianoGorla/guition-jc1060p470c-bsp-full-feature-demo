/*
 * Guition JC1060P470C Board Support Package - Implementation
 * Phase A: Power Manager + Phase D: Peripheral Drivers
 *
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "bsp_board.h"
#include "esp_log.h"
#include "bsp_log_panel.h"
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
#ifdef CONFIG_BSP_ENABLE_CAMERA
#include "drivers/ov02c10_wrapper.h"
#endif

/* Include I2C test utilities */
#if defined(CONFIG_DEBUG_I2C_GPIO_CHECK) || defined(CONFIG_DEBUG_I2C_TEST_PERIPHERALS) || defined(CONFIG_DEBUG_I2C_AUTO_RECOVERY)
#include "../utils/i2c_test.h"
#endif

#define LOG_UNIT "CORE"
#define LOGI(fmt, ...) BSP_LOGI_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) BSP_LOGW_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)

static const char *BSP_BANNER_LINE = "==============================================================";

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

/* Hardware handles */
static esp_lcd_panel_handle_t g_display_handle = NULL;
static esp_lcd_touch_handle_t g_touch_handle = NULL;

static void bsp_apply_external_log_filter(void)
{
#ifdef CONFIG_APP_REDUCE_EXTERNAL_LOG_NOISE
    esp_log_level_t level = ESP_LOG_WARN;
    const char *level_name = "WARN";

#if defined(CONFIG_APP_EXTERNAL_LOG_FILTER_ERROR)
    level = ESP_LOG_ERROR;
    level_name = "ERROR";
#elif defined(CONFIG_APP_EXTERNAL_LOG_FILTER_NONE)
    level = ESP_LOG_NONE;
    level_name = "NONE";
#endif

    const char *noisy_tags[] = {
        "jd9165",
        "GT911",
        "LVGL",
        "LVGL_INIT",
        "lvgl_demo",
    };

    for (size_t i = 0; i < sizeof(noisy_tags) / sizeof(noisy_tags[0]); i++) {
        esp_log_level_set(noisy_tags[i], level);
    }

    LOGI("[LOG] External log noise filter enabled (level=%s)", level_name);
#endif
}

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
            LOGI( "[RESET] Clean boot - no hard reset");
            return false;
        default:
            LOGW( "[RESET] Unsafe reset - hard reset required");
            return true;
    }
}

static void bsp_hard_reset(void)
{
    if (!bsp_needs_hard_reset()) return;

    LOGW( "[RESET] === HARD RESET ===");
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
    LOGI( "[PHASE A] Power Manager...");
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

#ifdef CONFIG_BSP_ENABLE_CAMERA
    ESP_ERROR_CHECK(bsp_camera_power_on());
    LOGI( "[PHASE A] [OK] Camera reset sequence");
#endif

    LOGI( "[PHASE A] [OK] POWER_READY");
    return ESP_OK;
}

static esp_err_t bsp_i2c_bus_init(void)
{
#if defined(CONFIG_BSP_ENABLE_TOUCH) || defined(CONFIG_BSP_ENABLE_AUDIO) || defined(CONFIG_BSP_ENABLE_RTC) || defined(CONFIG_BSP_ENABLE_CAMERA)
    /* Skip if already initialized */
    if (g_i2c_bus_handle != NULL) {
        LOGI( "[I2C] Already initialized");
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_BSP_I2C_SCL_GPIO,
        .sda_io_num = CONFIG_BSP_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &g_i2c_bus_handle));

    /* Enable verbose I2C logging if requested */
#ifdef CONFIG_DEBUG_I2C_VERBOSE
    esp_log_level_set("i2c", ESP_LOG_VERBOSE);
    esp_log_level_set("i2c.master", ESP_LOG_VERBOSE);
    LOGI( "[I2C] Verbose logging enabled");
#endif

    LOGI( "[I2C] [OK] Ready");

    /* Test peripherals after I2C init (if enabled) */
#ifdef CONFIG_DEBUG_I2C_TEST_PERIPHERALS
    i2c_test_peripherals(g_i2c_bus_handle);
#endif

#endif
    return ESP_OK;
}

static esp_err_t bsp_phase_d_peripheral_drivers(void)
{
    LOGI( "[PHASE D] Peripheral Drivers...");

    /*
     * CRITICAL: Initialize display FIRST, then I2C
     *
     * The JD9165 MIPI-DSI display controller corrupts the I2C peripheral
     * hardware during initialization. The solution is to initialize I2C
     * AFTER the display, not before.
     *
     * This approach matches the Espressif vendor BSP:
     * - bsp_display_new() - no I2C
     * - bsp_touch_new() - calls bsp_i2c_init()
     *
     * See: docs/I2C_MIPI_DSI_CONFLICT.md
     */
#ifdef CONFIG_BSP_ENABLE_DISPLAY
    g_display_handle = bsp_display_init();
    if (!g_display_handle) return ESP_FAIL;
    LOGI( "[PHASE D] [OK] Display HW");
#endif

    /* NOW initialize I2C after display is stable */
    ESP_ERROR_CHECK(bsp_i2c_bus_init());

#ifdef CONFIG_BSP_ENABLE_TOUCH
    g_touch_handle = bsp_touch_init();
    if (!g_touch_handle) return ESP_FAIL;
    LOGI( "[PHASE D] [OK] Touch HW");
#endif

#ifdef CONFIG_BSP_ENABLE_AUDIO
    bsp_audio_config_t audio_cfg = BSP_AUDIO_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(bsp_audio_init(&audio_cfg));
    LOGI( "[PHASE D] [OK] Audio");
#endif

#ifdef CONFIG_BSP_ENABLE_RTC
    ESP_ERROR_CHECK(bsp_rtc_init());
    LOGI( "[PHASE D] [OK] RTC");
#endif

#ifdef CONFIG_BSP_ENABLE_CAMERA
    esp_err_t cam_ret = bsp_camera_init();
    if (cam_ret == ESP_OK) {
        LOGI( "[PHASE D] [OK] Camera probe");
    } else {
        LOGW( "[PHASE D] [WARN] Camera probe failed: %s", esp_err_to_name(cam_ret));
    }
#endif

    LOGI( "[PHASE D] [OK] Complete");
    return ESP_OK;
}

esp_err_t bsp_board_init(void)
{
    bsp_apply_external_log_filter();

    LOGI("%s", BSP_BANNER_LINE);
    LOGI( "  Guition BSP v1.3.0");
    LOGI( "  Hardware Layer Only");
    LOGI("%s", BSP_BANNER_LINE);

    ESP_ERROR_CHECK(bsp_phase_a_power_manager());
    ESP_ERROR_CHECK(bsp_phase_d_peripheral_drivers());

    LOGI("%s", BSP_BANNER_LINE);
    LOGI( "  [OK] BSP Ready (Hardware only)");
    LOGI( "  App must init LVGL separately");
    LOGI("%s", BSP_BANNER_LINE);

    return ESP_OK;
}

esp_lcd_panel_handle_t bsp_display_get_handle(void)
{
    return g_display_handle;
}

esp_lcd_touch_handle_t bsp_touch_get_handle(void)
{
    return g_touch_handle;
}

i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void)
{
    return g_i2c_bus_handle;
}

void bsp_board_deinit(void)
{
#ifdef CONFIG_BSP_ENABLE_CAMERA
    bsp_camera_deinit();
#endif

    if (g_i2c_bus_handle) {
        i2c_del_master_bus(g_i2c_bus_handle);
        g_i2c_bus_handle = NULL;
    }
}
