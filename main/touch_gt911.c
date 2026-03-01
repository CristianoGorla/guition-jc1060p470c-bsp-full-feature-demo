#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "touch_gt911.h"

static const char *TAG = "GT911";

#define GT911_RST_GPIO         GPIO_NUM_22
#define GT911_INT_GPIO         GPIO_NUM_21

esp_lcd_touch_handle_t init_touch_gt911(i2c_master_bus_handle_t i2c_bus)
{
    ESP_LOGI(TAG, "Initializing GT911 touch controller");

    // Reset manuale PRIMA di comunicare via I2C
    // Questo setta l'indirizzo a 0x5D (INT=LOW durante reset)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GT911_RST_GPIO) | (1ULL << GT911_INT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Reset sequence per address 0x5D (INT LOW)
    gpio_set_level(GT911_INT_GPIO, 0);  // INT = LOW per 0x5D
    gpio_set_level(GT911_RST_GPIO, 0);  // RST = LOW
    vTaskDelay(pdMS_TO_TICKS(10));
    
    gpio_set_level(GT911_RST_GPIO, 1);  // RST = HIGH
    vTaskDelay(pdMS_TO_TICKS(5));
    
    gpio_set_level(GT911_INT_GPIO, 0);  // Keep INT LOW
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Rilascia INT come input per interrupt
    io_conf.pin_bit_mask = (1ULL << GT911_INT_GPIO);
    io_conf.mode = GPIO_MODE_INPUT;
    gpio_config(&io_conf);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "GT911 reset sequence completed (Address: 0x5D)");

    // Configurazione I2C con indirizzo 0x5D
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = 0x5D,  // Indirizzo dopo reset con INT=LOW
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .control_phase_bytes = 1,
        .lcd_cmd_bits = 16,  // GT911 usa 16-bit register address
        .lcd_param_bits = 8,
        .dc_bit_offset = 0,
        .flags = {
            .disable_control_phase = 0,
        },
        .scl_speed_hz = 400000,  // 400kHz
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(i2c_bus, &tp_io_config, &tp_io_handle));

    // GT911 touch config - DISABILITA il reset del driver (già fatto)
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 1024,
        .y_max = 600,
        .rst_gpio_num = GPIO_NUM_NC,  // -1: NON fare reset (già fatto manualmente)
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
