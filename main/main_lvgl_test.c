#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "LVGL_TEST";

/**
 * @brief Simple LVGL test UI
 * 
 * Creates a centered label and a touch-responsive button
 * to verify display + touch integration.
 */
void lvgl_create_test_ui(void)
{
#ifdef CONFIG_BSP_ENABLE_LVGL
    ESP_LOGI(TAG, "Creating LVGL test UI...");
    
    /* Get active screen */
    lv_obj_t *scr = lv_scr_act();
    
    /* Background color */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x003366), 0);
    
    /* Title label */
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, 
        "LVGL v9.2.2 Ready!\n"
        "Display: JD9165 (1024x600)\n"
        "Touch: GT911 (I2C 0x14)\n"
        "Memory: PSRAM 32MB @ 200MHz"
    );
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -80);
    
    /* Touch test button */
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 300, 100);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF6600), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF9933), LV_STATE_PRESSED);
    
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Touch Test");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_28, 0);
    lv_obj_center(btn_label);
    
    /* Version info (bottom left) */
    lv_obj_t *version_label = lv_label_create(scr);
    lv_label_set_text_fmt(version_label, "ESP32-P4 | BSP v1.3.0 | LVGL v%d.%d.%d",
                          LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    lv_obj_set_style_text_color(version_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(version_label, &lv_font_montserrat_14, 0);
    lv_obj_align(version_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    
    ESP_LOGI(TAG, "\u2713 LVGL test UI created");
#endif
}
