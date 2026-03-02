#include "rx8025t_bsp.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

static const char *TAG = "BSP_RX8025T";

/* Hardware Configuration */
#define RX8025T_I2C_ADDRESS    0x32
#define RX8025T_INT_GPIO       GPIO_NUM_0

/* RX8025T Register Addresses */
#define RX8025T_REG_SECOND     0x00
#define RX8025T_REG_MINUTE     0x01
#define RX8025T_REG_HOUR       0x02
#define RX8025T_REG_WEEKDAY    0x03
#define RX8025T_REG_DAY        0x04
#define RX8025T_REG_MONTH      0x05
#define RX8025T_REG_YEAR       0x06
#define RX8025T_REG_CTRL1      0x0E
#define RX8025T_REG_CTRL2      0x0F

/* External I2C handle */
extern i2c_master_bus_handle_t g_i2c_bus_handle;

static i2c_master_dev_handle_t g_rtc_dev_handle = NULL;

/**
 * @brief Convert decimal to BCD (Binary-Coded Decimal)
 */
static inline uint8_t dec2bcd(uint8_t dec)
{
    return ((dec / 10) << 4) | (dec % 10);
}

/**
 * @brief Convert BCD to decimal
 */
static inline uint8_t bcd2dec(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

esp_err_t bsp_rtc_init(void)
{
    ESP_LOGI(TAG, "Initializing RX8025T RTC");

    if (g_i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized! Call bsp_i2c_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    /* Configure interrupt GPIO as input */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RX8025T_INT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,  /* Configure interrupt later if needed */
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Failed to configure INT GPIO");

    /* Add RX8025T device to I2C bus */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RX8025T_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };
    
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_i2c_bus_handle, &dev_cfg, &g_rtc_dev_handle), TAG,
                        "Failed to add RX8025T to I2C bus");

    /* Initialize RTC control registers */
    uint8_t ctrl_data[2] = {RX8025T_REG_CTRL1, 0x20};  /* Enable 24-hour mode */
    ESP_RETURN_ON_ERROR(i2c_master_transmit(g_rtc_dev_handle, ctrl_data, sizeof(ctrl_data), 1000), TAG,
                        "Failed to configure RTC control register");

    ESP_LOGI(TAG, "RX8025T initialized (address 0x%02X, INT on GPIO %d)", 
             RX8025T_I2C_ADDRESS, RX8025T_INT_GPIO);
    return ESP_OK;
}

esp_err_t bsp_rtc_set_time(const bsp_rtc_time_t *time)
{
    if (g_rtc_dev_handle == NULL) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Prepare time data in BCD format */
    uint8_t data[8] = {
        RX8025T_REG_SECOND,          /* Start register */
        dec2bcd(time->second),
        dec2bcd(time->minute),
        dec2bcd(time->hour),
        dec2bcd(time->weekday),
        dec2bcd(time->day),
        dec2bcd(time->month),
        dec2bcd(time->year)
    };

    ESP_RETURN_ON_ERROR(i2c_master_transmit(g_rtc_dev_handle, data, sizeof(data), 1000), TAG,
                        "Failed to write RTC time");

    ESP_LOGI(TAG, "RTC time set: 20%02d-%02d-%02d %02d:%02d:%02d",
             time->year, time->month, time->day, time->hour, time->minute, time->second);
    return ESP_OK;
}

esp_err_t bsp_rtc_get_time(bsp_rtc_time_t *time)
{
    if (g_rtc_dev_handle == NULL) {
        ESP_LOGE(TAG, "RTC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Write start register address */
    uint8_t reg = RX8025T_REG_SECOND;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(g_rtc_dev_handle, &reg, 1, 1000), TAG,
                        "Failed to set register pointer");

    /* Read 7 time registers */
    uint8_t data[7];
    ESP_RETURN_ON_ERROR(i2c_master_receive(g_rtc_dev_handle, data, sizeof(data), 1000), TAG,
                        "Failed to read RTC time");

    /* Convert BCD to decimal */
    time->second = bcd2dec(data[0] & 0x7F);  /* Mask VLF bit */
    time->minute = bcd2dec(data[1] & 0x7F);
    time->hour = bcd2dec(data[2] & 0x3F);
    time->weekday = bcd2dec(data[3] & 0x07);
    time->day = bcd2dec(data[4] & 0x3F);
    time->month = bcd2dec(data[5] & 0x1F);
    time->year = bcd2dec(data[6]);

    return ESP_OK;
}

void bsp_rtc_tm_to_time(const struct tm *tm_time, bsp_rtc_time_t *rtc_time)
{
    rtc_time->second = tm_time->tm_sec;
    rtc_time->minute = tm_time->tm_min;
    rtc_time->hour = tm_time->tm_hour;
    rtc_time->day = tm_time->tm_mday;
    rtc_time->weekday = tm_time->tm_wday;
    rtc_time->month = tm_time->tm_mon + 1;  /* tm_mon is 0-11 */
    rtc_time->year = (tm_time->tm_year + 1900) - 2000;  /* tm_year is years since 1900 */
}

void bsp_rtc_time_to_tm(const bsp_rtc_time_t *rtc_time, struct tm *tm_time)
{
    tm_time->tm_sec = rtc_time->second;
    tm_time->tm_min = rtc_time->minute;
    tm_time->tm_hour = rtc_time->hour;
    tm_time->tm_mday = rtc_time->day;
    tm_time->tm_wday = rtc_time->weekday;
    tm_time->tm_mon = rtc_time->month - 1;  /* tm_mon is 0-11 */
    tm_time->tm_year = (rtc_time->year + 2000) - 1900;  /* tm_year is years since 1900 */
    tm_time->tm_isdst = -1;  /* Let system determine DST */
}
