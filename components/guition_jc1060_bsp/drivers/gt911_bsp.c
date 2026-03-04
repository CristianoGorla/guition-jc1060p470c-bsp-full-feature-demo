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

/* GT911 Registers for raw debugging */
#define GT911_REG_STATUS       0x814E  /* Status register */
#define GT911_REG_POINT1       0x814F  /* First touch point data */
#define GT911_REG_CONFIG       0x8047  /* Config register start */

/* External I2C handle (initialized by bsp_i2c_init) */
extern i2c_master_bus_handle_t g_i2c_bus_handle;

/* Global touch handle for debug task */
static esp_lcd_touch_handle_t g_touch_handle = NULL;
static TaskHandle_t g_touch_debug_task = NULL;

/**
 * @brief Read GT911 raw register for debugging
 */
static esp_err_t gt911_read_register(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t reg_addr[2] = {(reg >> 8) & 0xFF, reg & 0xFF};
    
    i2c_master_dev_handle_t dev_handle;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };
    
    if (i2c_master_bus_add_device(g_i2c_bus_handle, &dev_cfg, &dev_handle) != ESP_OK) {
        return ESP_FAIL;
    }
    
    esp_err_t ret = i2c_master_transmit_receive(dev_handle, reg_addr, 2, data, len, 100);
    i2c_master_bus_rm_device(dev_handle);
    
    return ret;
}

/**
 * @brief Dump raw bytes for debugging
 */
static void dump_hex(const char *label, const uint8_t *data, size_t len)
{
    char hex_str[128];
    char *p = hex_str;
    for (size_t i = 0; i < len && i < 20; i++) {
        p += sprintf(p, "%02X ", data[i]);
    }
    ESP_LOGI(TAG, "%s: %s", label, hex_str);
}

/**
 * @brief Aggressive touch debug task - polls GT911 status register
 */
static void touch_debug_task(void *arg)
{
    uint8_t status = 0;
    uint8_t point_data[40];  /* Read more data for analysis */
    uint32_t touch_count = 0;
    uint32_t last_status_log = 0;
    bool first_touch_logged = false;
    
    ESP_LOGI(TAG, "🔍 Touch debug task started - polling GT911 status register");
    ESP_LOGI(TAG, "Display resolution: %dx%d", TOUCH_MAX_X, TOUCH_MAX_Y);
    
    while (1) {
        /* Read status register */
        if (gt911_read_register(GT911_REG_STATUS, &status, 1) == ESP_OK) {
            uint8_t touch_points = status & 0x0F;
            bool buffer_status = (status >> 7) & 0x01;
            
            if (touch_points > 0) {
                /* Read ALL touch point data (8 bytes per point) */
                if (gt911_read_register(GT911_REG_POINT1, point_data, 40) == ESP_OK) {
                    
                    /* First touch: dump RAW bytes for analysis */
                    if (!first_touch_logged) {
                        ESP_LOGI(TAG, "📊 === FIRST TOUCH RAW DATA DUMP ===");
                        dump_hex("  Status reg (0x814E)", &status, 1);
                        dump_hex("  Point data (0x814F+)", point_data, 40);
                        first_touch_logged = true;
                    }
                    
                    /* Parse first touch point */
                    /* GT911 datasheet: each point is 8 bytes:
                     * [0-1]: X coordinate (little-endian)
                     * [2-3]: Y coordinate (little-endian)  
                     * [4-5]: Size (little-endian)
                     * [6]: Reserved
                     * [7]: Track ID
                     */
                    
                    /* Method 1: Little-endian (correct per datasheet) */
                    uint16_t x_le = point_data[0] | (point_data[1] << 8);
                    uint16_t y_le = point_data[2] | (point_data[3] << 8);
                    uint16_t size_le = point_data[4] | (point_data[5] << 8);
                    
                    /* Method 2: Big-endian (in case driver is wrong) */
                    uint16_t x_be = (point_data[0] << 8) | point_data[1];
                    uint16_t y_be = (point_data[2] << 8) | point_data[3];
                    uint16_t size_be = (point_data[4] << 8) | point_data[5];
                    
                    touch_count++;
                    
                    ESP_LOGI(TAG, "👆 TOUCH #%u: points=%d, buf=%d", touch_count, touch_points, buffer_status);
                    ESP_LOGI(TAG, "   LE: X=%4d Y=%4d size=%4d %s", 
                             x_le, y_le, size_le,
                             (x_le <= TOUCH_MAX_X && y_le <= TOUCH_MAX_Y) ? "✅ IN RANGE" : "❌ OUT OF RANGE");
                    ESP_LOGI(TAG, "   BE: X=%4d Y=%4d size=%4d %s", 
                             x_be, y_be, size_be,
                             (x_be <= TOUCH_MAX_X && y_be <= TOUCH_MAX_Y) ? "✅ IN RANGE" : "❌ OUT OF RANGE");
                    
                    /* Show raw bytes for first 8 bytes (one touch point) */
                    ESP_LOGI(TAG, "   RAW: [%02X %02X] [%02X %02X] [%02X %02X] [%02X %02X]",
                             point_data[0], point_data[1], point_data[2], point_data[3],
                             point_data[4], point_data[5], point_data[6], point_data[7]);
                }
                
                /* Clear status register */
                uint8_t clear = 0;
                uint8_t write_buf[3] = {(GT911_REG_STATUS >> 8) & 0xFF, GT911_REG_STATUS & 0xFF, clear};
                i2c_master_dev_handle_t dev_handle;
                i2c_device_config_t dev_cfg = {
                    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                    .device_address = TOUCH_I2C_ADDRESS,
                    .scl_speed_hz = 400000,
                };
                if (i2c_master_bus_add_device(g_i2c_bus_handle, &dev_cfg, &dev_handle) == ESP_OK) {
                    i2c_master_transmit(dev_handle, write_buf, 3, 100);
                    i2c_master_bus_rm_device(dev_handle);
                }
            } else {
                /* Log "no touch" periodically */
                uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
                if (now - last_status_log > 5000) {
                    ESP_LOGI(TAG, "ℹ️  GT911 status: 0x%02X (no touches, total: %u)", status, touch_count);
                    last_status_log = now;
                }
            }
        } else {
            ESP_LOGE(TAG, "❌ Failed to read GT911 status register!");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));  /* Poll every 50ms */
    }
}

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

esp_err_t bsp_touch_reset(void)
{
    ESP_LOGI(TAG, "Re-executing GT911 reset sequence (post I2C recovery)");
    return touch_reset_sequence();
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

    /* Store global handle for debug task */
    g_touch_handle = touch_handle;

    /* Start aggressive touch debug task */
    ESP_LOGI(TAG, "Starting aggressive touch debug task...");
    xTaskCreate(touch_debug_task, "touch_debug", 4096, NULL, 5, &g_touch_debug_task);

    return touch_handle;
}
