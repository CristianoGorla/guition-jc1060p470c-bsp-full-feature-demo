idf_component_register(
    SRCS "bsp_sensors.c"
    INCLUDE_DIRS "include"
    REQUIRES guition_jc1060_bsp driver freertos esp_timer
)
