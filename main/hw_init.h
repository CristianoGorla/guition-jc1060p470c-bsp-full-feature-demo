#ifndef HW_INIT_H
#define HW_INIT_H

#include "driver/gpio.h"
#include "esp_err.h"

// Pin definitions from schematic (Guition JC1060P470C_I_W_Y)
// Verified from complete schematic pages
#define PIN_TOUCHINT        GPIO_NUM_21  // GT911 INT pin (from FPC3)
#define PIN_TOUCHRST        GPIO_NUM_22  // GT911 RST pin (from FPC3)
#define PIN_I2C_SDA         GPIO_NUM_7   // I2C SDA (shared bus: GT911 + ES8311 + RTC)
#define PIN_I2C_SCL         GPIO_NUM_8   // I2C SCL (shared bus: GT911 + ES8311 + RTC)

// Sequenza di inizializzazione hardware
void hw_reset_all_peripherals(void);
void hw_reset_gt911_for_address_0x14(void);
void hw_reset_gt911_for_address_0x5D(void);

#endif // HW_INIT_H
