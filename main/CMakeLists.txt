# Main application component

idf_component_register(
    SRCS 
        "main.c"
        "bootstrap_manager.c"
        "display_jd9165.c"
        "touch_gt911.c"
        "i2c_utils.c"
        "es8311_audio.c"
        "rtc_rx8025t.c"
        "rtc_ntp_sync.c"
        "rtc_test.c"
        "esp_hosted_wifi.c"
        "sd_card_manager.c"
        "sd_card_functions.c"
        "sdmmc_arbiter.c"
    INCLUDE_DIRS 
        "."
        "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES 
        guition_jc1060_bsp
        lvgl
        nvs_flash
        esp_netif
        esp_event
        esp_wifi
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
