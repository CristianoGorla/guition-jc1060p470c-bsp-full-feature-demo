#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void init_touch_gt911(i2c_master_bus_handle_t i2c_bus)
{
    // Reset hardware Guition: INT=21, RST=22 [7]
    gpio_set_direction(22, GPIO_MODE_OUTPUT);
    gpio_set_direction(21, GPIO_MODE_OUTPUT);
    gpio_set_level(22, 0);
    gpio_set_level(21, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(22, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_direction(21, GPIO_MODE_INPUT);

    esp_lcd_panel_io_handle_t tp_io_handle;
    esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = 0x14, // Indirizzo sbloccato [History]
        .scl_speed_hz = 400000,
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 1024,
        .y_max = 600,
        .rst_gpio_num = -1,
        .int_gpio_num = 21,
    };
    esp_lcd_touch_handle_t touch_handle;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle));
}