set(srcs "src/esp_cam_sensor.c")
set(include_dirs "include")
set(priv_include_dirs "")
set(requires "driver" "esp_sccb_intf" "esp_driver_cam")
set(priv_requires "esp_driver_gpio" "esp_timer")

if(CONFIG_CAMERA_OV02C10)
    list(APPEND srcs "sensors/ov02c10/ov02c10.c")
    list(APPEND include_dirs "sensors/ov02c10/include")
    list(APPEND priv_include_dirs "sensors/ov02c10/private_include")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${include_dirs}
                       PRIV_INCLUDE_DIRS ${priv_include_dirs}
                       REQUIRES ${requires}
                       PRIV_REQUIRES ${priv_requires}
                       LDFRAGMENTS linker.lf)

if(CONFIG_CAMERA_OV02C10_AUTO_DETECT_MIPI_INTERFACE_SENSOR)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ov02c10_detect")
endif()

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})
