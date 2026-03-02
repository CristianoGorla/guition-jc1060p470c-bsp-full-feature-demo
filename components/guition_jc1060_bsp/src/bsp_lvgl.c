/**
 * @file bsp_lvgl.c
 * @brief LVGL Integration Implementation for Guition JC1060P470C BSP
 * 
 * This implementation uses esp_lvgl_port official component for LVGL 9.2.2.
 * Based on Espressif's ESP32-P4 Function EV Board BSP approach.
 */

#include "bsp_lvgl.h"
#include "bsp_board.h"
#include "jd9165_bsp.h"
#include "gt911_bsp.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

static const char *TAG = "BSP_LVGL";

/* Global handles */
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;

/* Display configuration from bsp_board_config.h */
#define BSP_LCD_H_RES                      (1024)
#define BSP_LCD_V_RES                      (600)
#define BSP_LCD_DRAW_BUFF_SIZE             (BSP_LCD_H_RES * 100)  // 100 lines buffer
#define BSP_LCD_DRAW_BUFF_DOUBLE           (true)
#define BSP_LCD_COLOR_FORMAT_RGB565        (true)  // RGB565 for performance

/**
 * @brief Initialize LVGL display using esp_lvgl_port
 * 
 * This function follows the vendor demo approach:
 * 1. Initialize LCD panel (JD9165 via bsp_display_init)
 * 2. Configure lvgl_port for DSI display
 * 3. Add display with lvgl_port_add_disp_dsi()
 * 4. Enable software rotation to avoid tearing
 */
static lv_display_t *bsp_lvgl_init_display(const bsp_lvgl_config_t *config)
{
    ESP_LOGI(TAG, "Initializing LVGL display...");

    /* Initialize LCD panel (JD9165 MIPI DSI) */
    lcd_panel = bsp_display_init();
    if (lcd_panel == NULL) {
        ESP_LOGE(TAG, "Failed to initialize LCD panel");
        return NULL;
    }
    ESP_LOGI(TAG, "✓ LCD panel initialized (JD9165 1024x600)");

    /* Configure LVGL port display settings */
    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = lcd_panel,
        .buffer_size = config->buffer_size,
        .double_buffer = config->double_buffer,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        /* Rotation must match hardware initial state */
        .rotation = {
            .swap_xy = false,    // Will use software rotation
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = config->buff_dma,
            .buff_spiram = config->buff_spiram,
            .swap_bytes = false,  // LSB first for RGB565
            .sw_rotate = config->sw_rotate,  // Enable SW rotation for smooth experience
#ifdef CONFIG_BSP_DISPLAY_LVGL_FULL_REFRESH
            .full_refresh = true,
#elif defined(CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE)
            .direct_mode = true,
#endif
        }
    };

    /* DSI-specific configuration for avoid tearing */
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = {
#ifdef CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR
            .avoid_tearing = true,
#else
            .avoid_tearing = false,
#endif
        }
    };

    /* Add DSI display to LVGL port */
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return NULL;
    }

    /* Apply initial rotation if specified */
    if (config->rotation != LV_DISPLAY_ROTATION_0) {
        lv_disp_set_rotation(disp, config->rotation);
        ESP_LOGI(TAG, "Display rotation set to %d degrees", config->rotation * 90);
    }

    ESP_LOGI(TAG, "✓ LVGL display added (buffer: %d px, double: %s)",
             config->buffer_size,
             config->double_buffer ? "yes" : "no");

    return disp;
}

/**
 * @brief Initialize LVGL touch input using esp_lvgl_port
 * 
 * Initializes GT911 touch controller and registers it with LVGL.
 */
static lv_indev_t *bsp_lvgl_init_touch(lv_display_t *disp)
{
    ESP_LOGI(TAG, "Initializing LVGL touch input...");

    /* Initialize GT911 touch controller */
    touch_handle = bsp_touch_init();
    if (touch_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize touch controller");
        return NULL;
    }
    ESP_LOGI(TAG, "✓ Touch controller initialized (GT911)");

    /* Add touch input to LVGL port */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = touch_handle,
    };

    lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg);
    if (indev == NULL) {
        ESP_LOGE(TAG, "Failed to add LVGL touch input");
        return NULL;
    }

    ESP_LOGI(TAG, "✓ LVGL touch input registered");
    return indev;
}

/* ========== Public API Implementation ========== */

lv_display_t *bsp_lvgl_start(void)
{
    bsp_lvgl_config_t default_cfg = BSP_LVGL_CONFIG_DEFAULT();
    return bsp_lvgl_start_with_config(&default_cfg);
}

lv_display_t *bsp_lvgl_start_with_config(const bsp_lvgl_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid configuration");
        return NULL;
    }

    if (lvgl_disp != NULL) {
        ESP_LOGW(TAG, "LVGL already started");
        return lvgl_disp;
    }

    ESP_LOGI(TAG, "========== LVGL Initialization ==========");

    /* Initialize LVGL port */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    esp_err_t ret = lvgl_port_init(&lvgl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LVGL port: %s", esp_err_to_name(ret));
        return NULL;
    }
    ESP_LOGI(TAG, "✓ LVGL port initialized (v%d.%d.%d)",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    /* Initialize display */
    lvgl_disp = bsp_lvgl_init_display(config);
    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "Failed to initialize LVGL display");
        goto err_cleanup;
    }

    /* Initialize touch input */
    lvgl_touch_indev = bsp_lvgl_init_touch(lvgl_disp);
    if (lvgl_touch_indev == NULL) {
        ESP_LOGE(TAG, "Failed to initialize LVGL touch");
        goto err_cleanup;
    }

    /* Set display brightness to 100% */
    ret = bsp_display_set_brightness(100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set brightness: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "✓ LVGL initialization complete!");
    ESP_LOGI(TAG, "  Display: %dx%d @ %s", BSP_LCD_H_RES, BSP_LCD_V_RES,
             config->double_buffer ? "double-buffer" : "single-buffer");
    ESP_LOGI(TAG, "  Touch: GT911 multi-touch");
    ESP_LOGI(TAG, "  Use bsp_lvgl_lock() before LVGL calls");
    ESP_LOGI(TAG, "========================================");

    return lvgl_disp;

err_cleanup:
    lvgl_disp = NULL;
    lvgl_touch_indev = NULL;
    return NULL;
}

lv_indev_t *bsp_lvgl_get_touch_input(void)
{
    return lvgl_touch_indev;
}

bool bsp_lvgl_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_lvgl_unlock(void)
{
    lvgl_port_unlock();
}

void bsp_lvgl_rotate(lv_display_t *disp, lv_disp_rotation_t rotation)
{
    if (disp == NULL) {
        ESP_LOGE(TAG, "Invalid display handle");
        return;
    }

    lv_disp_set_rotation(disp, rotation);
    ESP_LOGI(TAG, "Display rotation changed to %d degrees", rotation * 90);
}
