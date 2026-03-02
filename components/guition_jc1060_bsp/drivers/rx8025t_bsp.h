/**
 * @file rx8025t_bsp.h
 * @brief RX8025T Real-Time Clock Driver for BSP
 * 
 * Hardware Configuration:
 * - I2C Address: 0x32
 * - Interrupt: GPIO 0 (alarm/timer output)
 * - Crystal: 32.768 kHz
 * - Temperature Compensation: Yes
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <time.h>
#include <stdbool.h>

/**
 * @brief RTC time structure
 */
typedef struct {
    uint8_t second;   /* 0-59 */
    uint8_t minute;   /* 0-59 */
    uint8_t hour;     /* 0-23 */
    uint8_t day;      /* 1-31 */
    uint8_t weekday;  /* 0-6 (Sunday=0) */
    uint8_t month;    /* 1-12 */
    uint8_t year;     /* 0-99 (offset from 2000) */
} bsp_rtc_time_t;

/**
 * @brief Initialize RX8025T RTC
 * 
 * @note Requires I2C bus to be initialized first (bsp_i2c_init)
 * @return ESP_OK on success
 */
esp_err_t bsp_rtc_init(void);

/**
 * @brief Set RTC time
 * 
 * @param time Time structure to set
 * @return ESP_OK on success
 */
esp_err_t bsp_rtc_set_time(const bsp_rtc_time_t *time);

/**
 * @brief Get RTC time
 * 
 * @param time Pointer to store time
 * @return ESP_OK on success
 */
esp_err_t bsp_rtc_get_time(bsp_rtc_time_t *time);

/**
 * @brief Convert struct tm to bsp_rtc_time_t
 */
void bsp_rtc_tm_to_time(const struct tm *tm_time, bsp_rtc_time_t *rtc_time);

/**
 * @brief Convert bsp_rtc_time_t to struct tm
 */
void bsp_rtc_time_to_tm(const bsp_rtc_time_t *rtc_time, struct tm *tm_time);

#ifdef __cplusplus
}
#endif
