#include "gt911_bsp.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_touch_gt911.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_GT911";

/* Hardware Pin Configuration */
#define TOUCH_I2C_ADDRESS      0x14  /* Forced via reset sequence */
#define TOUCH_RESET_GPIO       GPIO_NUM_21
#define TOUCH_INT_GPIO         GPIO_NUM_22

/* Touch Panel Resolution (matches display) */
#define TOUCH_MAX_X            1024
#define TOUCH_MAX_Y            600
#define TOUCH_MAX_POINTS       5

/* External I2C handle (initialized by bsp_i2c_init) */
extern i2c_master_bus_handle_t g_i2c_bus_handle;

/**
 * @brief Perform GT911 reset sequence to force I2C address to 0x14
 * 
 * CRITICAL: The GT911 I2C address is determined by the INT pin state during reset:
 * - INT = LOW during reset → Address 0x14 (0x28 in 8-bit format)
 * - INT = HIGH during reset → Address 0x5D (0xBA in 8-bit format)
 * 
 * Reset Sequence (from GT911 datasheet):
 * 1. Hold INT LOW
 * 2. Pull RST LOW (reset active)
 * 3. Wait 10ms
 * 4. Release RST HIGH (reset inactive)
 * 5. Wait 5ms
 * 6. Release INT (return to interrupt mode)
 * 7. Wait 50ms for touch controller initialization
 */
static esp_err_t touch_reset_sequence(void)
{
    ESP_LOGI(TAG, "Starting GT911 reset sequence (forcing address 0x%02X)", TOUCH_I2C_ADDRESS);

    /* Configure INT pin as output (temporarily) */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TOUCH_INT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Failed to configure INT GPIO");

    /* Configure RESET pin as output */
    io_conf.pin_bit_mask = (1ULL << TOUCH_RESET_GPIO);
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Failed to configure RESET GPIO");

    /* Step 1-2: INT=LOW, RST=LOW */
    gpio_set_level(TOUCH_INT_GPIO, 0);
    gpio_set_level(TOUCH_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Step 4: RST=HIGH (release reset) */
    gpio_set_level(TOUCH_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(5));

    /* Step 6: Return INT to input mode (interrupt function) */
    io_conf.pin_bit_mask = (1ULL << TOUCH_INT_GPIO);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;  /* INT has internal pull-up */
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Failed to reconfigure INT GPIO");

    /* Step 7: Wait for touch controller initialization */
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "Reset sequence complete, GT911 address set to 0x%02X", TOUCH_I2C_ADDRESS);
    return ESP_OK;
}

esp_lcd_touch_handle_t bsp_touch_init(void)
{
    ESP_LOGI(TAG, "Initializing GT911 touch controller");

    if (g_i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized! Call bsp_i2c_init() first");
        return NULL;
    }

    /* CRITICAL: Perform reset sequence BEFORE any I2C communication */
    if (touch_reset_sequence() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to execute reset sequence");
        return NULL;
    }

    esp_lcd_touch_handle_t touch_handle = NULL;

    /* Configure I2C panel IO for touch controller */
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = 400000;  /* 400kHz I2C clock */

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(g_i2c_bus_handle, &tp_io_config, &tp_io_handle));

    /* Configure GT911 touch panel */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = TOUCH_MAX_X,
        .y_max = TOUCH_MAX_Y,
        .rst_gpio_num = TOUCH_RESET_GPIO,
        .int_gpio_num = TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,      /* Active low reset */
            .interrupt = 0,  /* Active low interrupt */
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle));

    ESP_LOGI(TAG, "GT911 initialized (address 0x%02X, %dx%d, %d points)",
             TOUCH_I2C_ADDRESS, TOUCH_MAX_X, TOUCH_MAX_Y, TOUCH_MAX_POINTS);

    return touch_handle;
}
