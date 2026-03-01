#ifndef I2C_UTILS_H
#define I2C_UTILS_H

#include "driver/i2c_master.h"
#include "esp_log.h"

/**
 * @brief Scan I2C bus for devices and display results
 * @param bus_handle I2C master bus handle
 */
void i2c_scan_bus(i2c_master_bus_handle_t bus_handle);

/**
 * @brief Check I2C bus health with detailed diagnostics
 * @param bus_handle I2C master bus handle
 * @return true if bus is healthy, false otherwise
 */
bool i2c_check_bus_health(i2c_master_bus_handle_t bus_handle);

#endif // I2C_UTILS_H
