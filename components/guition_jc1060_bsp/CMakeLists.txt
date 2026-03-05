# Build source list based on Kconfig
set(COMPONENT_SRCS
    "src/bsp_board.c"
    "src/bsp_heartbeat.c"
    "src/bsp_tests.c"
)

# Display driver (always needed if BSP enabled)
if(CONFIG_BSP_ENABLE_DISPLAY)
    list(APPEND COMPONENT_SRCS "drivers/jd9165_bsp.c")
    list(APPEND COMPONENT_SRCS "utils/backlight_test.c")
endif()

# Touch driver
if(CONFIG_BSP_ENABLE_TOUCH)
    list(APPEND COMPONENT_SRCS "drivers/gt911_bsp.c")
endif()

# Audio driver
if(CONFIG_BSP_ENABLE_AUDIO)
    list(APPEND COMPONENT_SRCS "drivers/es8311_bsp.c")
endif()

# RTC driver
if(CONFIG_BSP_ENABLE_RTC)
    list(APPEND COMPONENT_SRCS "drivers/rx8025t_bsp.c")
    list(APPEND COMPONENT_SRCS "utils/rtc_test.c")
endif()

# I2C test utilities (if any I2C debug feature enabled)
if(CONFIG_DEBUG_I2C_GPIO_CHECK OR CONFIG_DEBUG_I2C_TEST_PERIPHERALS)
    list(APPEND COMPONENT_SRCS "utils/i2c_test.c")
endif()

# Bootstrap manager (WiFi/SD)
if(CONFIG_BSP_ENABLE_WIFI OR CONFIG_BSP_ENABLE_SDCARD)
    list(APPEND COMPONENT_SRCS "bootstrap/bootstrap_manager.c")
    list(APPEND COMPONENT_SRCS "bootstrap/sdmmc_arbiter.c")
endif()

# SD Card specific
if(CONFIG_BSP_ENABLE_SDCARD)
    list(APPEND COMPONENT_SRCS "bootstrap/sd_card_manager.c")
    list(APPEND COMPONENT_SRCS "utils/sd_card_functions.c")
endif()

# WiFi specific
if(CONFIG_BSP_ENABLE_WIFI)
    list(APPEND COMPONENT_SRCS "bootstrap/esp_hosted_wifi.c")
endif()

# RTC NTP sync (requires both RTC and WiFi)
if(CONFIG_APP_ENABLE_RTC_NTP_SYNC)
    list(APPEND COMPONENT_SRCS "utils/rtc_ntp_sync.c")
endif()

# Build component requirements based on Kconfig
set(COMPONENT_REQUIRES
    driver
    freertos
    nvs_flash
)

if(CONFIG_BSP_ENABLE_DISPLAY)
    list(APPEND COMPONENT_REQUIRES esp_lcd esp_lcd_jd9165)
endif()

if(CONFIG_BSP_ENABLE_TOUCH)
    list(APPEND COMPONENT_REQUIRES esp_lcd_touch_gt911)
endif()

# LVGL moved to main component - BSP only handles hardware

if(CONFIG_BSP_ENABLE_SDCARD)
    list(APPEND COMPONENT_REQUIRES sdmmc fatfs)
endif()

if(CONFIG_BSP_ENABLE_WIFI)
    list(APPEND COMPONENT_REQUIRES esp_netif esp_wifi lwip)
endif()

if(CONFIG_BSP_ENABLE_RTC OR CONFIG_BSP_ENABLE_AUDIO)
    list(APPEND COMPONENT_REQUIRES esp_timer)
endif()

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS
        "include"
        "drivers"
        "bootstrap"
        "utils"
    REQUIRES ${COMPONENT_REQUIRES}
)
