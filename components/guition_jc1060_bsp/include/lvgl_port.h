#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LVGL graphics library
 * 
 * Configures LVGL v9.2.2 with:
 * - JD9165 display driver (1024x600, MIPI DSI)
 * - GT911 touch controller (I2C 0x14)
 * - PSRAM double buffering (32MB @ 200MHz)
 * - FreeRTOS tick handler task
 * 
 * @return ESP_OK on success
 */
esp_err_t bsp_lvgl_init(void);

/**
 * @brief LVGL tick handler task
 * 
 * Periodically calls lv_timer_handler() every 5ms.
 * Created automatically by bsp_lvgl_init().
 * 
 * @param arg Unused
 */
void lvgl_tick_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif // LVGL_PORT_H
