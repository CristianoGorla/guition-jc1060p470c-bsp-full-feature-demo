#ifndef I2C_UTILS_H
#define I2C_UTILS_H

#include "driver/i2c_master.h"
#include "esp_log.h"

void i2c_scan_bus(i2c_master_bus_handle_t bus_handle);

#endif // I2C_UTILS_H
