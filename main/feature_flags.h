#ifndef FEATURE_FLAGS_H
#define FEATURE_FLAGS_H

/*
 * Feature Flags - Abilita/Disabilita periferiche e debug
 * Imposta a 1 per abilitare, 0 per disabilitare
 */

// ========== SD CARD ==========
#define ENABLE_SD_CARD 1 // SD card mount (FUNZIONA!)
#define DEBUG_SD_CARD 1  // Log dettagliati SD card

// ========== WIFI/ESP-HOSTED ==========
#define ENABLE_WIFI 1 // ESP-Hosted WiFi (FUNZIONA!)
#define DEBUG_WIFI 1  // Log dettagliati WiFi

// ========== I2C BUS ==========
#define ENABLE_I2C 1      // I2C bus initialization (TEST CON SD+WIFI)
#define DEBUG_I2C 1       // Log dettagliati I2C
#define ENABLE_I2C_SCAN 1 // Scan bus I2C (SENZA HALT)

// ========== DISPLAY ==========
#define ENABLE_DISPLAY 0      // JD9165 MIPI DSI display (disabilitato per ora)
#define DEBUG_DISPLAY 0       // Log dettagliati display
#define ENABLE_DISPLAY_TEST 0 // Test pattern RGB

// ========== TOUCH ==========
#define ENABLE_TOUCH 0      // GT911 touch (disabilitato per ora)
#define DEBUG_TOUCH 0       // Log dettagliati touch
#define ENABLE_TOUCH_TEST 0 // Test touch input

// ========== NVS ==========
#define ENABLE_NVS 1 // NVS Flash storage
#define DEBUG_NVS 0  // Log dettagliati NVS

/*
 * Helper macros per log condizionali
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
