#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "touch_gt911.h"

static const char *TAG = "GT911";

#define GT911_I2C_ADDRESS      0x14  // Cambiato da 0x5D -> Usa 0x14
#define GT911_RST_GPIO         GPIO_NUM_22
#define GT911_INT_GPIO         GPIO_NUM_21

esp_lcd_touch_handle_t init_touch_gt911(i2c_master_bus_handle_t i2c_bus)
{
    ESP_LOGI(TAG, "Initializing GT911 touch controller");

    // Hardware reset sequence per settare indirizzo 0x14
    // INT HIGH durante reset -> Address 0x14
    // INT LOW durante reset -> Address 0x5D
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GT911_RST_GPIO) | (1ULL << GT911_INT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Reset sequence per address 0x14 (INT HIGH)
    gpio_set_level(GT911_INT_GPIO, 1);  // INT = HIGH per 0x14
    gpio_set_level(GT911_RST_GPIO, 0);  // RST = LOW
    vTaskDelay(pdMS_TO_TICKS(20));
    
    gpio_set_level(GT911_RST_GPIO, 1);  // RST = HIGH
    vTaskDelay(pdMS_TO_TICKS(5));
    
    gpio_set_level(GT911_INT_GPIO, 1);  // Keep INT HIGH
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Rilascia INT come input per interrupt
    io_conf.pin_bit_mask = (1ULL << GT911_INT_GPIO);
    io_conf.mode = GPIO_MODE_INPUT;
    gpio_config(&io_conf);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "GT911 reset sequence completed (Address: 0x%02X)", GT911_I2C_ADDRESS);

    // Create panel IO handle for I2C
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = GT911_I2C_ADDRESS,
        .scl_speed_hz = 400000,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 0,
        .lcd_param_bits = 8,
        .dc_bit_offset = 0,
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(i2c_bus, &tp_io_config, &tp_io_handle));

    // GT911 touch config
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 1024,
        .y_max = 600,
        .rst_gpio_num = -1,  // Reset already done
        .int_gpio_num = GT911_INT_GPIO,
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
    
    ESP_LOGI(TAG, "GT911 touch initialized successfully");
    
    return touch_handle;
}
