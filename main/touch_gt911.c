#include "esp_lcd_touch_gt911.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "GT911";

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

    // Configurazione I2C per GT911
    esp_lcd_touch_io_i2c_config_t io_config = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
        .scl_speed_hz = 400000,
    };

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

    esp_lcd_touch_handle_t touch_handle;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(i2c_bus, &io_config, &tp_cfg, &touch_handle));
    
    ESP_LOGI(TAG, "GT911 touch initialized");
}
