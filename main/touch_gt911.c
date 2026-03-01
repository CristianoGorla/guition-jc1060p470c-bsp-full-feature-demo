#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "touch_gt911.h"

static const char *TAG = "GT911";

// Pin definitions from schematic (Guition JC1060P470C_I_W_Y)
#define GT911_RST_GPIO         GPIO_NUM_19  // TOUCHRST
#define GT911_INT_GPIO         GPIO_NUM_20  // TOUCHINT
#define GT911_I2C_ADDRESS      0x14  // Address with INT=HIGH during reset

esp_lcd_touch_handle_t init_touch_gt911(i2c_master_bus_handle_t i2c_bus)
{
    ESP_LOGI(TAG, "Initializing GT911 touch controller");
    ESP_LOGI(TAG, "Using I2C address: 0x%02X (hardware reset already done in hw_init)", GT911_I2C_ADDRESS);

    // NON fare reset qui - già fatto in hw_init.c per address 0x14
    // Il chip è già configurato con INT=HIGH durante reset

    // Configurazione I2C con indirizzo 0x14
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = GT911_I2C_ADDRESS,  // 0x14 (INT=HIGH durante reset)
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
    
    esp_err_t ret = esp_lcd_new_panel_io_i2c_v2(i2c_bus, &tp_io_config, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel I/O (0x%x)", ret);
        return NULL;
    }

    // GT911 touch config - DISABILITA il reset del driver (già fatto in hw_init)
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 1024,
        .y_max = 600,
        .rst_gpio_num = GPIO_NUM_NC,  // -1: NON fare reset (già fatto in hw_init)
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
    ret = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GT911 touch handle (0x%x)", ret);
        esp_lcd_panel_io_del(tp_io_handle);
        return NULL;
    }
    
    ESP_LOGI(TAG, "GT911 touch initialized successfully at 0x%02X", GT911_I2C_ADDRESS);
    ESP_LOGI(TAG, "Touch resolution: %dx%d", tp_cfg.x_max, tp_cfg.y_max);
    
    return touch_handle;
}
