# Main application component

idf_component_register(
    SRCS 
        "main.c"
        "lvgl_init.c"
        "lvgl_demo.c"
        "src/lvgl_dashboard.c"
        "backlight_test.c"
    INCLUDE_DIRS 
        "."
        "include"
        "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES 
        guition_jc1060_bsp
        nvs_flash
        esp_netif
        esp_event
        esp_wifi
        fatfs
        driver
        lvgl
        esp_lvgl_port
    PRIV_REQUIRES
        esp_timer
        sdmmc
)

# Generate build info AFTER component registration to avoid early evaluation issues
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)

if(NOT DEFINED BUILD_GIT_COMMIT)
    execute_process(
        COMMAND git log -1 --format=%h
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE BUILD_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT BUILD_GIT_COMMIT)
        set(BUILD_GIT_COMMIT "unknown")
    endif()
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/build_info.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/build_info.h
    @ONLY
)
