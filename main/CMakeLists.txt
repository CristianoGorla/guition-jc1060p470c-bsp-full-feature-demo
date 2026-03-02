# Main application component

# Generate build timestamp
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)

# Get git commit hash
if(NOT DEFINED ENV{BUILD_GIT_COMMIT})
    execute_process(
        COMMAND git log -1 --format=%h
        OUTPUT_VARIABLE BUILD_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT BUILD_GIT_COMMIT)
        set(BUILD_GIT_COMMIT "unknown")
    endif()
else()
    set(BUILD_GIT_COMMIT $ENV{BUILD_GIT_COMMIT})
endif()

# Generate build_info.h from template
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/build_info.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/build_info.h
    @ONLY
)

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
)
