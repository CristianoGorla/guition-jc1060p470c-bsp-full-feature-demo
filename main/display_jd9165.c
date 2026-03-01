#include "esp_log.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_jd9165.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_ldo_regulator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "JD9165_GUITION";

#define BSP_LCD_BACKLIGHT     (GPIO_NUM_23)
#define BSP_LCD_RST           (GPIO_NUM_0)
#define LCD_LEDC_CH           0

// Init sequence dal BSP vendor
static const jd9165_lcd_init_cmd_t lcd_cmd[] = {
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0xF7, (uint8_t[]){0x49,0x61,0x02,0x00}, 4, 0},
    {0x30, (uint8_t[]){0x01}, 1, 0},
    {0x04, (uint8_t[]){0x0C}, 1, 0},
    {0x05, (uint8_t[]){0x00}, 1, 0},
    {0x06, (uint8_t[]){0x00}, 1, 0},
    {0x0B, (uint8_t[]){0x11}, 1, 0},
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x20, (uint8_t[]){0x04}, 1, 0},
    {0x1F, (uint8_t[]){0x05}, 1, 0},
    {0x23, (uint8_t[]){0x00}, 1, 0},
    {0x25, (uint8_t[]){0x19}, 1, 0},
    {0x28, (uint8_t[]){0x18}, 1, 0},
    {0x29, (uint8_t[]){0x04}, 1, 0},
    {0x2A, (uint8_t[]){0x01}, 1, 0},
    {0x2B, (uint8_t[]){0x04}, 1, 0},
    {0x2C, (uint8_t[]){0x01}, 1, 0},
    {0x30, (uint8_t[]){0x02}, 1, 0},
    {0x01, (uint8_t[]){0x22}, 1, 0},
    {0x03, (uint8_t[]){0x12}, 1, 0},
    {0x04, (uint8_t[]){0x00}, 1, 0},
    {0x05, (uint8_t[]){0x64}, 1, 0},
    {0x0A, (uint8_t[]){0x08}, 1, 0},
    {0x0B, (uint8_t[]){0x0A,0x1A,0x0B,0x0D,0x0D,0x11,0x10,0x06,0x08,0x1F,0x1D}, 11, 0},
    {0x0C, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x0D, (uint8_t[]){0x16,0x1B,0x0B,0x0D,0x0D,0x11,0x10,0x07,0x09,0x1E,0x1C}, 11, 0},
    {0x0E, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x0F, (uint8_t[]){0x16,0x1B,0x0D,0x0B,0x0D,0x11,0x10,0x1C,0x1E,0x09,0x07}, 11, 0},
    {0x10, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x11, (uint8_t[]){0x0A,0x1A,0x0D,0x0B,0x0D,0x11,0x10,0x1D,0x1F,0x08,0x06}, 11, 0},
    {0x12, (uint8_t[]){0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D}, 11, 0},
    {0x14, (uint8_t[]){0x00,0x00,0x11,0x11}, 4, 0},
    {0x18, (uint8_t[]){0x99}, 1, 0},
    {0x30, (uint8_t[]){0x06}, 1, 0},
    {0x12, (uint8_t[]){0x36,0x2C,0x2E,0x3C,0x38,0x35,0x35,0x32,0x2E,0x1D,0x2B,0x21,0x16,0x29}, 14, 0},
    {0x13, (uint8_t[]){0x36,0x2C,0x2E,0x3C,0x38,0x35,0x35,0x32,0x2E,0x1D,0x2B,0x21,0x16,0x29}, 14, 0},
    {0x30, (uint8_t[]){0x0A}, 1, 0},
    {0x02, (uint8_t[]){0x4F}, 1, 0},
    {0x0B, (uint8_t[]){0x40}, 1, 0},
    {0x12, (uint8_t[]){0x3E}, 1, 0},
    {0x13, (uint8_t[]){0x78}, 1, 0},
    {0x30, (uint8_t[]){0x0D}, 1, 0},
    {0x0D, (uint8_t[]){0x04}, 1, 0},
    {0x10, (uint8_t[]){0x0C}, 1, 0},
    {0x11, (uint8_t[]){0x0C}, 1, 0},
    {0x12, (uint8_t[]){0x0C}, 1, 0},
    {0x13, (uint8_t[]){0x0C}, 1, 0},
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0X3A, (uint8_t[]){0x55}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 20},
};

static esp_err_t bsp_display_brightness_init(void)
{
    // Setup LEDC peripheral for PWM backlight control
    const ledc_channel_config_t LCD_backlight_channel = {
        .gpio_num = BSP_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = 1,
        .duty = 0,
        .hpoint = 0
    };
    const ledc_timer_config_t LCD_backlight_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = 1,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK
    };

    ESP_ERROR_CHECK(ledc_timer_config(&LCD_backlight_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&LCD_backlight_channel));
    return ESP_OK;
}

static esp_err_t bsp_enable_dsi_phy_power(void)
{
    // Turn on the power for MIPI DSI PHY
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_mipi_cfg = {
        .chan_id = 3,
        .voltage_mv = 2500,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_cfg, &ldo_mipi_phy));
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
    return ESP_OK;
}

void init_jd9165_display(void)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG, "=== INIZIO INIT DISPLAY ===");

    // Step 1: Brightness/backlight init (LEDC PWM)
    ESP_LOGI(TAG, ">>> Step 1: Init brightness controller");
    ret = bsp_display_brightness_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Brightness init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, ">>> Step 1: OK");

    // Step 2: Enable DSI PHY power (LDO3)
    ESP_LOGI(TAG, ">>> Step 2: Enable DSI PHY power");
    ret = bsp_enable_dsi_phy_power();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DSI PHY power failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, ">>> Step 2: OK");

    // Step 3: Create MIPI DSI bus
    ESP_LOGI(TAG, ">>> Step 3: Create MIPI DSI bus");
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 750,
    };
    ret = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "New DSI bus failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, ">>> Step 3: OK");

    // Step 4: Install MIPI DSI LCD control panel (DBI interface)
    ESP_LOGI(TAG, ">>> Step 4: Install LCD control panel");
    esp_lcd_panel_io_handle_t io;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ret = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "New panel IO DBI failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, ">>> Step 4: OK");

    // Step 5: Install JD9165 panel driver
    ESP_LOGI(TAG, ">>> Step 5: Install JD9165 panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    
    esp_lcd_dpi_panel_config_t dpi_config = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 52,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = 1024,
            .v_size = 600,
            .hsync_back_porch = 160,
            .hsync_pulse_width = 24,
            .hsync_front_porch = 160,
            .vsync_back_porch = 21,
            .vsync_pulse_width = 2,
            .vsync_front_porch = 12,
        },
        .flags = {
            .use_dma2d = true,
        }
    };

    jd9165_vendor_config_t vendor_config = {
        .init_cmds = lcd_cmd,
        .init_cmds_size = sizeof(lcd_cmd) / sizeof(jd9165_lcd_init_cmd_t),
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    
    esp_lcd_panel_dev_config_t lcd_dev_config = {
        .bits_per_pixel = 16,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .reset_gpio_num = BSP_LCD_RST,
        .vendor_config = &vendor_config,
    };
    
    ret = esp_lcd_new_panel_jd9165(io, &lcd_dev_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "New panel JD9165 failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, ">>> Step 5: OK");

    // Step 6: Reset panel
    ESP_LOGI(TAG, ">>> Step 6: Reset panel");
    ret = esp_lcd_panel_reset(panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel reset failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, ">>> Step 6: OK");

    // Step 7: Init panel
    ESP_LOGI(TAG, ">>> Step 7: Init panel");
    ret = esp_lcd_panel_init(panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, ">>> Step 7: OK");

    // Step 8: Turn on backlight
    ESP_LOGI(TAG, ">>> Step 8: Turn on backlight");
    uint32_t duty_cycle = 1023; // 100% brightness (10-bit resolution)
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH);
    ESP_LOGI(TAG, ">>> Step 8: OK");

    ESP_LOGI(TAG, "=== DISPLAY JD9165 PRONTO (1024x600 @ 52MHz) ===");
}
