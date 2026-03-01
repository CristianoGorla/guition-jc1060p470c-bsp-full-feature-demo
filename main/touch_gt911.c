#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "GT911";

#define GT911_I2C_ADDRESS  0x5D

void init_touch_gt911(i2c_master_bus_handle_t i2c_bus)
{
    ESP_LOGI(TAG, "Initializing GT911 touch controller");

    // Reset hardware Guition: INT=21, RST=22
    gpio_set_direction(22, GPIO_MODE_OUTPUT);
    gpio_set_direction(21, GPIO_MODE_OUTPUT);
    gpio_set_level(22, 0);
    gpio_set_level(21, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(22, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_direction(21, GPIO_MODE_INPUT);

    // Create panel IO handle for I2C
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_PANEL_IO_I2C_CONFIG();
    tp_io_config.dev_addr = GT911_I2C_ADDRESS;
    tp_io_config.scl_speed_hz = 400000;
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(i2c_bus, &tp_io_config, &tp_io_handle));

    // GT911 touch config
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 1024,
        .y_max = 600,
        .rst_gpio_num = GPIO_NUM_22,
        .int_gpio_num = GPIO_NUM_21,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    esp_lcd_touch_handle_t touch_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle));
    
    ESP_LOGI(TAG, "GT911 touch initialized");
}
