#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool initialized;
    bool present;
    bool moving_target;
    bool stationary_target;
    uint16_t distance_cm;
    uint16_t moving_distance_cm;
    uint16_t stationary_distance_cm;
    uint8_t status;
    uint64_t sample_count;
    int64_t timestamp_ms;
} bsp_radar_data_t;

esp_err_t bsp_radar_init(void);
esp_err_t bsp_radar_get_data(bsp_radar_data_t *out_data);
const bsp_radar_data_t *bsp_radar_get_data_ptr(void);
void bsp_radar_deinit(void);

#ifdef __cplusplus
}
#endif
