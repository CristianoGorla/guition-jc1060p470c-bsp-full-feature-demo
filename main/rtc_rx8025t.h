#ifndef RTC_RX8025T_H
#define RTC_RX8025T_H

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <time.h>

// RX8025T I2C Address (7-bit)
#define RX8025T_I2C_ADDR    0x32

// RX8025T Register Map
#define RX8025T_REG_SEC     0x00  // Seconds (BCD)
#define RX8025T_REG_MIN     0x01  // Minutes (BCD)
#define RX8025T_REG_HOUR    0x02  // Hours (BCD)
#define RX8025T_REG_WDAY    0x03  // Week day
#define RX8025T_REG_DAY     0x04  // Day (BCD)
#define RX8025T_REG_MONTH   0x05  // Month (BCD)
#define RX8025T_REG_YEAR    0x06  // Year (BCD, 00-99)
#define RX8025T_REG_OFFSET  0x07  // Digital offset
#define RX8025T_REG_ALARMW_MIN  0x08  // Alarm W Minutes
#define RX8025T_REG_ALARMW_HOUR 0x09  // Alarm W Hours
#define RX8025T_REG_ALARMW_WDAY 0x0A  // Alarm W Week day
#define RX8025T_REG_ALARMD_MIN  0x0B  // Alarm D Minutes
#define RX8025T_REG_ALARMD_HOUR 0x0C  // Alarm D Hours
#define RX8025T_REG_CTRL1   0x0E  // Control Register 1
#define RX8025T_REG_CTRL2   0x0F  // Control Register 2 (Extension)

// Control Register 1 bits
#define RX8025T_CTRL1_24H   (1 << 5)  // 24-hour format
#define RX8025T_CTRL1_TEST  (1 << 4)  // Test mode (must be 0)

// Control Register 2 (Extension) bits
#define RX8025T_CTRL2_PON   (1 << 1)  // Power-On flag
#define RX8025T_CTRL2_VLF   (1 << 0)  // Voltage Low flag

// RTC time structure
typedef struct {
    uint8_t second;   // 0-59
    uint8_t minute;   // 0-59
    uint8_t hour;     // 0-23
    uint8_t wday;     // 0-6 (Sunday=0)
    uint8_t day;      // 1-31
    uint8_t month;    // 1-12
    uint8_t year;     // 0-99 (2000-2099)
} rtc_time_t;

/**
 * Initialize RX8025T RTC
 * Clears PON/VLF flags, sets 24h format, starts oscillator
 */
esp_err_t rtc_rx8025t_init(i2c_master_bus_handle_t bus_handle);

/**
 * Set RTC time
 */
esp_err_t rtc_rx8025t_set_time(const rtc_time_t *time);

/**
 * Get RTC time
 */
esp_err_t rtc_rx8025t_get_time(rtc_time_t *time);

/**
 * Check if RTC lost power (PON flag)
 */
esp_err_t rtc_rx8025t_check_power_on_flag(bool *pon_flag);

/**
 * Check if voltage was low (VLF flag)
 */
esp_err_t rtc_rx8025t_check_voltage_low_flag(bool *vlf_flag);

#endif // RTC_RX8025T_H
