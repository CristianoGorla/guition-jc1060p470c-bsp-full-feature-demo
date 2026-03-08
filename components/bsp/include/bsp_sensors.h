#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	float temperature_c;
	float humidity_pct;
	float pressure_hpa;
	bool has_temperature;
	bool has_humidity;
	bool has_pressure;
} bsp_sensor_data_t;

esp_err_t bsp_sensors_init(i2c_master_bus_handle_t i2c_bus_handle);
esp_err_t bsp_sensor_get_data(bsp_sensor_data_t *out_data);
float bsp_sensor_get_temp(void);

#ifdef __cplusplus
}
#endif
