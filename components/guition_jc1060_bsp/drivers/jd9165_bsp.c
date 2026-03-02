// (Add at top with other static vars)
static esp_lcd_panel_io_handle_t g_dsi_io_handle = NULL;

// (In bsp_display_init, after esp_lcd_new_panel_io_dbi)
esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &g_dsi_io_handle);

// (Add new function at end)
esp_lcd_panel_io_handle_t bsp_jd9165_get_io(void)
{
    return g_dsi_io_handle;
}
