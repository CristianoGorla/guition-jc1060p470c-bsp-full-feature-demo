# Guition JC1060P470C BSP Component Build Configuration

idf_component_register(
    SRCS "src/bsp_board.c"
    INCLUDE_DIRS "include"
    REQUIRES 
        esp_lcd
        sdmmc
        fatfs
        lwip
        esp_netif
        esp_hosted
    PRIV_REQUIRES 
        driver
        esp_driver_gpio
        esp_driver_i2c
)
