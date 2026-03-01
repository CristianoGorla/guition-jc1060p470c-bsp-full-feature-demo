#include "hw_init.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HW_INIT";

/**
 * Reset GT911 per usare indirizzo 0x14 (INT=HIGH durante reset)
 * Usato quando il display ha il GT911 cablato per 0x14
 */
void hw_reset_gt911_for_address_0x14(void)
{
    ESP_LOGI(TAG, "GT911 reset sequence for address 0x14 (INT=HIGH)...");
    
    // Configura GPIO come output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_TOUCHRST) | (1ULL << PIN_TOUCHINT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Reset sequence per address 0x14 (INT HIGH)
    gpio_set_level(PIN_TOUCHINT, 1);  // INT = HIGH per 0x14
    gpio_set_level(PIN_TOUCHRST, 0);  // RST = LOW
    vTaskDelay(pdMS_TO_TICKS(10));
    
    gpio_set_level(PIN_TOUCHRST, 1);  // RST = HIGH
    vTaskDelay(pdMS_TO_TICKS(5));
    
    gpio_set_level(PIN_TOUCHINT, 1);  // Keep INT HIGH
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Rilascia INT come input per interrupt
    io_conf.pin_bit_mask = (1ULL << PIN_TOUCHINT);
    io_conf.mode = GPIO_MODE_INPUT;
    gpio_config(&io_conf);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "GT911 configured for address 0x14");
}

/**
 * Reset GT911 per usare indirizzo 0x5D (INT=LOW durante reset)
 * Indirizzo di default GT911
 */
void hw_reset_gt911_for_address_0x5D(void)
{
    ESP_LOGI(TAG, "GT911 reset sequence for address 0x5D (INT=LOW)...");
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_TOUCHRST) | (1ULL << PIN_TOUCHINT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Reset sequence per address 0x5D (INT LOW)
    gpio_set_level(PIN_TOUCHINT, 0);  // INT = LOW per 0x5D
    gpio_set_level(PIN_TOUCHRST, 0);  // RST = LOW
    vTaskDelay(pdMS_TO_TICKS(10));
    
    gpio_set_level(PIN_TOUCHRST, 1);  // RST = HIGH
    vTaskDelay(pdMS_TO_TICKS(5));
    
    gpio_set_level(PIN_TOUCHINT, 0);  // Keep INT LOW
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Rilascia INT come input per interrupt
    io_conf.pin_bit_mask = (1ULL << PIN_TOUCHINT);
    io_conf.mode = GPIO_MODE_INPUT;
    gpio_config(&io_conf);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "GT911 configured for address 0x5D");
}

/**
 * Reset completo di tutte le periferiche I2C
 * Chiamare PRIMA di inizializzare il bus I2C
 */
void hw_reset_all_peripherals(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "   HARDWARE INITIALIZATION");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    
    // 1. GT911 Touch Controller
    // Lo schematic mostra che è trovato a 0x14, quindi usa INT=HIGH
    ESP_LOGI(TAG, "[1/3] GT911 Touch Controller reset...");
    hw_reset_gt911_for_address_0x14();
    ESP_LOGI(TAG, "      Expected I2C address: 0x14");
    ESP_LOGI(TAG, "");
    
    // 2. ES8311 Audio Codec
    // Non ha pin di reset dedicato, si inizializza via I2C
    ESP_LOGI(TAG, "[2/3] ES8311 Audio Codec");
    ESP_LOGI(TAG, "      No hardware reset pin (I2C init only)");
    ESP_LOGI(TAG, "      Expected I2C address: 0x18");
    ESP_LOGI(TAG, "");
    
    // 3. RX8025T RTC
    // Non ha pin di reset, sempre attivo
    ESP_LOGI(TAG, "[3/3] RX8025T RTC");
    ESP_LOGI(TAG, "      No hardware reset pin (always active)");
    ESP_LOGI(TAG, "      Expected I2C address: 0x32 (7-bit: 0x19)");
    ESP_LOGI(TAG, "");
    
    // Note sulla camera CSI
    ESP_LOGI(TAG, "NOTE: CSI Camera on MIPI CSI slot (not on I2C bus)");
    ESP_LOGI(TAG, "");
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Hardware reset complete");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    
    // Attendi stabilizzazione finale
    vTaskDelay(pdMS_TO_TICKS(100));
}
