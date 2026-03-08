#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_sensors_init(void);
float bsp_sensor_get_temp(void);

#ifdef __cplusplus
}
#endif
