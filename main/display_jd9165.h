#pragma once

#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Inizializza il display JD9165 tramite MIPI DSI
     * @return Handle al pannello LCD per operazioni di drawing
     */
    esp_lcd_panel_handle_t init_jd9165_display(void);

#ifdef __cplusplus
}
#endif