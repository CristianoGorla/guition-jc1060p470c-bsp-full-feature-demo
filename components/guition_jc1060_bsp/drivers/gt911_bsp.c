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

/* 
 * HARDWARE TEST MODE: RST and INT pins disabled
 * Original mapping: RST=GPIO_NUM_21, INT=GPIO_NUM_22
 * Disabled to test if pin conflicts affect LVGL touch response
 */
// #define TOUCH_RESET_GPIO       GPIO_NUM_21  /* Original: GPIO 21 */
// #define TOUCH_INT_GPIO         GPIO_NUM_22  /* Original: GPIO 22 */
#define TOUCH_RESET_GPIO       GPIO_NUM_NC  /* TEST: Disabled for HW isolation */
#define TOUCH_INT_GPIO         GPIO_NUM_NC  /* TEST: Disabled for HW isolation */

/* Touch Panel Resolution (matches display) */
#define TOUCH_MAX_X            1024
#define TOUCH_MAX_Y            600
#define TOUCH_MAX_POINTS       5

/* GT911 Registers for raw debugging */
#define GT911_REG_STATUS       0x814E  /* Status register */
#define GT911_REG_POINT1       0x814F  /* First touch point data */
#define GT911_REG_CONFIG       0x8047  /* Config register start */
#define GT911_REG_X_MAX_LOW    0x8048  /* X resolution low byte */
#define GT911_REG_X_MAX_HIGH   0x8049  /* X resolution high byte */
#define GT911_REG_Y_MAX_LOW    0x804A  /* Y resolution low byte */
#define GT911_REG_Y_MAX_HIGH   0x804B  /* Y resolution high byte */
#define GT911_REG_TOUCH_NUM    0x804C  /* Max touch points */

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
 * @brief Read and display GT911 configuration registers
 */
static void read_gt911_config(void)
{
    uint8_t config_data[16];
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========== GT911 CONFIGURATION ==========");
    
    /* Read configuration block (0x8047-0x804C) */
    if (gt911_read_register(GT911_REG_CONFIG, config_data, 6) == ESP_OK) {
        uint16_t x_max = config_data[1] | (config_data[2] << 8);
        uint16_t y_max = config_data[3] | (config_data[4] << 8);
        uint8_t touch_num = config_data[5];
        
        ESP_LOGI(TAG, "Config Version: 0x%02X", config_data[0]);
        ESP_LOGI(TAG, "X Resolution: %d (expected %d) %s", x_max, TOUCH_MAX_X, 
                 (x_max == TOUCH_MAX_X) ? "✅" : "❌ MISMATCH!");
        ESP_LOGI(TAG, "Y Resolution: %d (expected %d) %s", y_max, TOUCH_MAX_Y,
                 (y_max == TOUCH_MAX_Y) ? "✅" : "❌ MISMATCH!");
        ESP_LOGI(TAG, "Max Touch Points: %d (expected %d) %s", touch_num, TOUCH_MAX_POINTS,
                 (touch_num == TOUCH_MAX_POINTS) ? "✅" : "❌ MISMATCH!");
        
        /* Display raw config bytes */
        dump_hex("  Config bytes (0x8047-0x804C)", config_data, 6);
        
        if (x_max != TOUCH_MAX_X || y_max != TOUCH_MAX_Y) {
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "⚠️  RESOLUTION MISMATCH DETECTED!");
            ESP_LOGE(TAG, "⚠️  GT911 is configured for %dx%d but display is %dx%d", 
                     x_max, y_max, TOUCH_MAX_X, TOUCH_MAX_Y);
            ESP_LOGE(TAG, "⚠️  Touch coordinates will be WRONG!");
            ESP_LOGE(TAG, "⚠️  Solution: Write correct resolution to GT911 config");
            ESP_LOGE(TAG, "");
        }
    } else {
        ESP_LOGE(TAG, "❌ Failed to read GT911 configuration!");
    }
    
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "");
}

/**
 * @brief Minimal touch debug task - only periodic summary
 * 
 * NOTE: Currently DISABLED to prevent console spam.
 *       LVGL handles touch polling automatically via esp_lcd_touch driver.
 *       This task was for debugging only.
 */
static void touch_debug_task(void *arg)
{
    uint8_t status = 0;
    uint8_t point_data[40];
    uint32_t touch_count = 0;
    uint32_t last_summary = 0;
    bool first_touch_logged = false;
    
    ESP_LOGI(TAG, "✅ Touch monitor started (periodic summary every 5s)");
    
    while (1) {
        /* Read status register */
        if (gt911_read_register(GT911_REG_STATUS, &status, 1) == ESP_OK) {
            uint8_t touch_points = status & 0x0F;
            
            if (touch_points > 0) {
                /* Read touch point data */
                if (gt911_read_register(GT911_REG_POINT1, point_data, 40) == ESP_OK) {
                    
                    /* First touch: dump RAW bytes for analysis */
                    if (!first_touch_logged) {
                        ESP_LOGI(TAG, "📊 === FIRST TOUCH RAW DATA ===");
                        dump_hex("  Status", &status, 1);
                        dump_hex("  Point data", point_data, 20);
                        
                        uint16_t x = point_data[1] | (point_data[2] << 8);
                        uint16_t y = point_data[3] | (point_data[4] << 8);
                        ESP_LOGI(TAG, "  First touch: X=%d Y=%d", x, y);
                        first_touch_logged = true;
                        
                        /* Read config on first touch */
                        read_gt911_config();
                    }
                    
                    touch_count++;
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
            }
            
            /* Periodic summary (every 5 seconds) */
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (now - last_summary > 5000) {
                ESP_LOGI(TAG, "📊 Touch summary: %u touches detected", touch_count);
                last_summary = now;
            }
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
 * 
 * NOTE: In HW test mode (GPIO_NUM_NC), this function does nothing.
 */
static esp_err_t touch_reset_sequence(void)
{
    /* Skip reset if pins are disabled (GPIO_NUM_NC) */
    if (TOUCH_RESET_GPIO == GPIO_NUM_NC || TOUCH_INT_GPIO == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "⚠️  Reset sequence SKIPPED (RST/INT pins disabled for HW test)");
        ESP_LOGW(TAG, "⚠️  GT911 will use default I2C address (may be 0x14 or 0x5D)");
        return ESP_OK;
    }
    
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
    ESP_LOGI(TAG, "⚠️  HARDWARE TEST MODE: RST=GPIO_NUM_NC, INT=GPIO_NUM_NC");
    ESP_LOGI(TAG, "⚠️  Pin manipulation disabled for isolation testing");

    if (g_i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized! Call bsp_i2c_init() first");
        return NULL;
    }

    /* CRITICAL: Perform reset sequence BEFORE any I2C communication */
    /* In HW test mode, this will skip pin manipulation */
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
        .rst_gpio_num = TOUCH_RESET_GPIO,  /* GPIO_NUM_NC in test mode */
        .int_gpio_num = TOUCH_INT_GPIO,    /* GPIO_NUM_NC in test mode */
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

    /* Read GT911 configuration to verify settings */
    read_gt911_config();

    /* Store global handle for debug task */
    g_touch_handle = touch_handle;

    /* DISABLED: Touch monitor task causes console spam */
    /* LVGL already polls touch via esp_lcd_touch driver */
    // ESP_LOGI(TAG, "Starting touch monitor task...");
    // xTaskCreate(touch_debug_task, "touch_monitor", 4096, NULL, 5, &g_touch_debug_task);
    ESP_LOGI(TAG, "Touch monitor task disabled (LVGL handles polling)");

    return touch_handle;
}
