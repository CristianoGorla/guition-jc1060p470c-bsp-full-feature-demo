idf_component_register(
    SRCS "bsp_sensors.c"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos esp_timer
)
