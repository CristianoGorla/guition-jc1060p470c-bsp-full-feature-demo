#include "esp_log.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_jd9165.h"
#include "driver/gpio.h"
#include "display_jd9165.h"

static const char *TAG = "JD9165_V55";

void init_jd9165_display(void)
{
    // Backlight: GPIO 23 (LCD_PWM schema Guition) [1]
    gpio_set_direction(23, GPIO_MODE_OUTPUT);
    gpio_set_level(23, 1);

    // 1. Bus MIPI DSI (2 lane obbligatorie per JD9165BA) [2]
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 2,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = 800,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    // 2. Control Plane: DBI over DSI (API v5.5) [History]
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io_handle));

    // 3. Timing Video DPI (Pixel Clock 51.2 MHz dal dtsi) [3]
    esp_lcd_video_timing_t video_timing = {
        .h_size = 1024,
        .v_size = 600,
        .hsync_back_porch = 136,
        .hsync_front_porch = 160,
        .hsync_pulse_width = 24,
        .vsync_back_porch = 21,
        .vsync_front_porch = 12,
        .vsync_pulse_width = 2,
    };

    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 51200000,
        .video_timing = video_timing,
        .in_color_format = LCD_COLOR_FMT_RGB888,
    };

    // 4. Inizializzazione Pannello JD9165
    esp_lcd_panel_handle_t panel_handle = NULL;
    jd9165_vendor_config_t vendor_config = {
        .mipi_config = {.dsi_bus = mipi_dsi_bus, .dpi_config = &dpi_config}};
    esp_lcd_panel_dev_config_t panel_dev_config = {
        .reset_gpio_num = -1,
        .color_space = LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 24,
        .vendor_config = &vendor_config};

    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9165(io_handle, &panel_dev_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    ESP_LOGI(TAG, "Display inizializzato correttamente.");
}