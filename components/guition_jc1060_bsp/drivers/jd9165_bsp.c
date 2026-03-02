#include "jd9165_bsp.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_jd9165.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_ldo_regulator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_JD9165";

/* Hardware Pin Configuration */
#define LCD_BACKLIGHT_GPIO     GPIO_NUM_23
#define LCD_RESET_GPIO         GPIO_NUM_0
#define LCD_LEDC_CHANNEL       0
#define LCD_MIPI_DSI_LANE_NUM  2

/* Display Timing Configuration (from MTK QD070AS01 datasheet) */
#define LCD_H_RES              1024
#define LCD_V_RES              600
#define LCD_PIXEL_CLOCK_MHZ    52    // ~51.2MHz nominal
#define LCD_HSYNC_BACK_PORCH   136
#define LCD_HSYNC_FRONT_PORCH  160
#define LCD_HSYNC_PULSE_WIDTH  24
#define LCD_VSYNC_BACK_PORCH   21
#define LCD_VSYNC_FRONT_PORCH  12
#define LCD_VSYNC_PULSE_WIDTH  2

/* JD9165 initialization sequence (from Guition BSP vendor) */
static const jd9165_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0xF7, (uint8_t[]){0x49,0x61,0x02,0x00}, 4, 0},
    {0x30, (uint8_t[]){0x01}, 1, 0},
    {0x04, (uint8_t[]){0x0C}, 1, 0},
    {0x05, (uint8_t[]){0x00}, 1, 0},
    {0x06, (uint8_t[]){0x00}, 1, 0},
    {0x0B, (uint8_t[]){0x11}, 1, 0},  /* CRITICAL: 0x11 = 2 lanes */
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x20, (uint8_t[]){0x04}, 1, 0},
    {0x1F, (uint8_t[]){0x05}, 1, 0},  /* hs_settle time = 5 */
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
    {0X3A, (uint8_t[]){0x55}, 1, 0},  /* RGB565 format */
    {0x11, (uint8_t[]){0x00}, 1, 120}, /* CRITICAL: Sleep OUT + 120ms delay */
    {0x29, (uint8_t[]){0x00}, 1, 20},  /* CRITICAL: Display ON + 20ms delay */
};

static ledc_channel_t g_bl_channel = LCD_LEDC_CHANNEL;
static esp_lcd_panel_io_handle_t g_dsi_io_handle = NULL;  // ✓ Store for LVGL

static esp_err_t backlight_init(void)
{
    const ledc_channel_config_t bl_channel = {
        .gpio_num = LCD_BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = g_bl_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = 1,
        .duty = 0,
        .hpoint = 0
    };
    const ledc_timer_config_t bl_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = 1,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK
    };

    ESP_RETURN_ON_ERROR(ledc_timer_config(&bl_timer), TAG, "Failed to config backlight timer");
    ESP_RETURN_ON_ERROR(ledc_channel_config(&bl_channel), TAG, "Failed to config backlight channel");
    
    ESP_LOGI(TAG, "Backlight PWM initialized (GPIO %d, 20kHz)", LCD_BACKLIGHT_GPIO);
    return ESP_OK;
}

static esp_err_t dsi_phy_power_on(void)
{
    static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = 3,
        .voltage_mv = 2500,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan), TAG, 
                        "Failed to acquire LDO channel for DSI PHY");
    ESP_LOGI(TAG, "MIPI DSI PHY powered on (LDO3 @ 2.5V)");
    return ESP_OK;
}

esp_lcd_panel_handle_t bsp_display_init(void)
{
    ESP_LOGI(TAG, "Initializing JD9165 display (1024x600, 2-lane DSI)");

    ESP_ERROR_CHECK(backlight_init());
    ESP_ERROR_CHECK(dsi_phy_power_on());

    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    esp_lcd_panel_handle_t disp_panel = NULL;

    /* Configure 2-lane MIPI DSI bus @ 750 Mbps (51.2MHz pixel clock equivalent) */
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = LCD_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 750,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    /* Configure DBI interface for control commands */
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &g_dsi_io_handle));  // ✓ Store handle

    /* Configure DPI interface for pixel data */
    esp_lcd_dpi_panel_config_t dpi_config = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = LCD_PIXEL_CLOCK_MHZ,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = LCD_H_RES,
            .v_size = LCD_V_RES,
            .hsync_back_porch = LCD_HSYNC_BACK_PORCH,
            .hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH,
            .hsync_front_porch = LCD_HSYNC_FRONT_PORCH,
            .vsync_back_porch = LCD_VSYNC_BACK_PORCH,
            .vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH,
            .vsync_front_porch = LCD_VSYNC_FRONT_PORCH,
        },
        .flags = {
            .use_dma2d = true,
        }
    };

    /* JD9165 vendor-specific configuration */
    jd9165_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(jd9165_lcd_init_cmd_t),
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    
    esp_lcd_panel_dev_config_t lcd_dev_config = {
        .bits_per_pixel = 16,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .reset_gpio_num = LCD_RESET_GPIO,
        .vendor_config = &vendor_config,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9165(g_dsi_io_handle, &lcd_dev_config, &disp_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(disp_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(disp_panel));

    /* Set backlight to 100% brightness */
    bsp_display_set_brightness(100);

    ESP_LOGI(TAG, "Display initialized successfully");
    return disp_panel;
}

esp_lcd_panel_io_handle_t bsp_jd9165_get_io(void)
{
    return g_dsi_io_handle;
}

esp_err_t bsp_display_set_brightness(uint8_t brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }
    
    /* Convert percentage to 10-bit duty cycle (0-1023) */
    uint32_t duty = (brightness_percent * 1023) / 100;
    
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, g_bl_channel, duty), TAG,
                        "Failed to set backlight duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, g_bl_channel), TAG,
                        "Failed to update backlight");
    
    return ESP_OK;
}
