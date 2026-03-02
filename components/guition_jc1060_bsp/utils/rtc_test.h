#ifndef RTC_TEST_H
#define RTC_TEST_H

#include "driver/i2c_master.h"
#include "esp_err.h"

/**
 * Comprehensive RTC hardware test
 * Tests multiple I2C speeds, addresses, and read/write patterns
 * 
 * @param bus_handle I2C bus handle
 */
void rtc_hardware_test(i2c_master_bus_handle_t bus_handle);

/**
 * Test RTC at specific address and speed
 * 
 * @param bus_handle I2C bus handle
 * @param addr I2C address to test
 * @param speed_hz I2C speed in Hz
 * @return ESP_OK if RTC responds
 */
esp_err_t rtc_test_at_address(i2c_master_bus_handle_t bus_handle, uint8_t addr, uint32_t speed_hz);

/**
 * Try to read RTC registers without any write
 * Most gentle approach possible
 * 
 * @param bus_handle I2C bus handle
 * @return ESP_OK if read succeeds
 */
esp_err_t rtc_test_read_only(i2c_master_bus_handle_t bus_handle);

/**
 * Test different I2C speeds
 * 
 * @param bus_handle I2C bus handle
 */
void rtc_test_speeds(i2c_master_bus_handle_t bus_handle);

#endif // RTC_TEST_H
