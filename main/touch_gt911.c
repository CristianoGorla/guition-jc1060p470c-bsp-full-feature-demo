#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "touch_gt911.h"

static const char *TAG = "GT911";

// Pin definitions from Guition JC1060P470C schematic
// Note: In 1024x600 config, driver manages reset automatically via I2C
#define GT911_RST_GPIO         GPIO_NUM_21  // TOUCH_RST
#define GT911_INT_GPIO         GPIO_NUM_22  // TOUCH_INT

esp_lcd_touch_handle_t init_touch_gt911(i2c_master_bus_handle_t i2c_bus)
{
    ESP_LOGI(TAG, "Initializing GT911 touch controller");
    ESP_LOGI(TAG, "Using driver auto-reset and auto-detect address (0x14/0x5D)");

    /* Create I2C panel IO for GT911 - uses auto address detection */
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = 400000;  // 400kHz I2C
    
    esp_err_t ret = esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GT911 panel I/O (0x%x)", ret);
        return NULL;
    }

    /* GT911 touch config - driver handles reset sequence internally */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 1024,
        .y_max = 600,
        .rst_gpio_num = GT911_RST_GPIO,  // Driver will pulse this for correct address
        .int_gpio_num = GT911_INT_GPIO,  // Interrupt pin
        .levels = {
            .reset = 0,      // Active LOW reset
            .interrupt = 0,  // Active LOW interrupt
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    /* Initialize GT911 - driver executes reset sequence and detects address */
    esp_lcd_touch_handle_t touch_handle = NULL;
    ret = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GT911 touch handle (0x%x)", ret);
        ESP_LOGE(TAG, "Check:");
        ESP_LOGE(TAG, "  1. I2C bus initialized (GPIO7/8)");
        ESP_LOGE(TAG, "  2. Touch pins connected (GPIO21=RST, GPIO22=INT)");
        ESP_LOGE(TAG, "  3. GT911 powered and not in reset");
        esp_lcd_panel_io_del(tp_io_handle);
        return NULL;
    }
    
    ESP_LOGI(TAG, "✓ GT911 initialized successfully");
    ESP_LOGI(TAG, "  Resolution: %dx%d", tp_cfg.x_max, tp_cfg.y_max);
    ESP_LOGI(TAG, "  Driver auto-detected I2C address");
    ESP_LOGI(TAG, "  Touch ready for reading");
    
    return touch_handle;
}
