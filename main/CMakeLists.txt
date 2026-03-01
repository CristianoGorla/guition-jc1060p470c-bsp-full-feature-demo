idf_component_register(SRCS "sd_card_example_main.c" 
                            "sd_card_functions.c" 
                            "esp_hosted_wifi.c"
                            "display_jd9165.c"
                            "touch_gt911.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_i2c esp_driver_gpio sdmmc vfs fatfs esp_wifi esp_event esp_netif lwip esp_hosted esp_lcd nvs_flash)