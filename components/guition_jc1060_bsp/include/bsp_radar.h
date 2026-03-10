#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_RADAR_PROFILE_RESOLUTION_CM       20U
#define BSP_RADAR_PROFILE_MOVING_GATE_MIN     0U
#define BSP_RADAR_PROFILE_MOVING_GATE_MAX     0U
#define BSP_RADAR_PROFILE_STATIONARY_GATE_MAX 0U
#define BSP_RADAR_PROFILE_NO_ONE_WINDOW_S     1U
#define BSP_RADAR_PROFILE_SAMPLE_PERIOD_MS    50U

typedef struct {
    bool initialized;
    bool present;
    bool moving_target;
    bool stationary_target;
    bool out_pin_high;
    uint16_t distance_cm;
    uint16_t moving_distance_cm;
    uint16_t stationary_distance_cm;
    uint8_t moving_signal_pct;
    uint8_t stationary_signal_pct;
    uint8_t energy_pct;
    uint8_t status;
    uint64_t interrupt_count;
    int64_t interrupt_timestamp_ms;
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
