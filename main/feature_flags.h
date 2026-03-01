#ifndef FEATURE_FLAGS_H
#define FEATURE_FLAGS_H

/*
 * Feature Flags - Enable/Disable peripherals and debug
 * Set to 1 to enable, 0 to disable
 */

// ========== SD CARD ==========
#define ENABLE_SD_CARD 0 // SD card mount
#define DEBUG_SD_CARD 0  // Detailed SD logs

// ========== WIFI/ESP-HOSTED ==========
#define ENABLE_WIFI 0 // ESP-Hosted WiFi via C6
#define DEBUG_WIFI 0  // Detailed WiFi logs

// ========== I2C BUS ==========
#define ENABLE_I2C 1      // I2C bus (required for Touch/Audio/RTC)
#define DEBUG_I2C 1       // Detailed I2C logs
#define ENABLE_I2C_SCAN 1 // Scan I2C bus for devices

// ========== AUDIO CODEC ==========
#define ENABLE_AUDIO 0 // ES8311 audio codec
#define DEBUG_AUDIO 0  // Detailed audio logs

// ========== RTC ==========
#define ENABLE_RTC 0         // RX8025T RTC
#define DEBUG_RTC 0          // Detailed RTC logs
#define ENABLE_RTC_TEST 0    // Test RTC read/write
#define ENABLE_RTC_HW_TEST 0 // Hardware diagnostic

// ========== DISPLAY ==========
#define ENABLE_DISPLAY 1      // JD9165 MIPI DSI display (1024x600)
#define DEBUG_DISPLAY 1       // Detailed display logs
#define ENABLE_DISPLAY_TEST 1 // RGB test pattern

// ========== TOUCH ==========
#define ENABLE_TOUCH 1      // GT911 capacitive touch
#define DEBUG_TOUCH 1       // Detailed touch logs
#define ENABLE_TOUCH_TEST 1 // Touch input test (continuous)

// ========== NVS ==========
#define ENABLE_NVS 1 // NVS Flash (required for some peripherals)
#define DEBUG_NVS 0  // Detailed NVS logs

/*
 * Helper macros for conditional logging
 */
#if DEBUG_SD_CARD
#define LOG_SD(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#else
#define LOG_SD(tag, format, ...) \
    do                           \
    {                            \
    } while (0)
#endif

#if DEBUG_WIFI
#define LOG_WIFI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#else
#define LOG_WIFI(tag, format, ...) \
    do                             \
    {                              \
    } while (0)
#endif

#if DEBUG_I2C
#define LOG_I2C(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#else
#define LOG_I2C(tag, format, ...) \
    do                            \
    {                             \
    } while (0)
#endif

#if DEBUG_AUDIO
#define LOG_AUDIO(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#else
#define LOG_AUDIO(tag, format, ...) \
    do                              \
    {                               \
    } while (0)
#endif

#if DEBUG_RTC
#define LOG_RTC(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#else
#define LOG_RTC(tag, format, ...) \
    do                            \
    {                             \
    } while (0)
#endif

#if DEBUG_DISPLAY
#define LOG_DISPLAY(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#else
#define LOG_DISPLAY(tag, format, ...) \
    do                                \
    {                                 \
    } while (0)
#endif

#if DEBUG_TOUCH
#define LOG_TOUCH(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#else
#define LOG_TOUCH(tag, format, ...) \
    do                              \
    {                               \
    } while (0)
#endif

#if DEBUG_NVS
#define LOG_NVS(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#else
#define LOG_NVS(tag, format, ...) \
    do                            \
    {                             \
    } while (0)
#endif

#endif // FEATURE_FLAGS_H
