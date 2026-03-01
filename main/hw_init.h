#ifndef HW_INIT_H
#define HW_INIT_H

#include "driver/gpio.h"
#include "esp_err.h"

// Pin definitions from schematic (Guition JC1060P470C_I_W_Y)
#define PIN_TOUCHINT        GPIO_NUM_20  // GT911 INT pin
#define PIN_TOUCHRST        GPIO_NUM_19  // GT911 RST pin
#define PIN_I2C_SDA         GPIO_NUM_7   // I2C SDA (shared bus)
#define PIN_I2C_SCL         GPIO_NUM_8   // I2C SCL (shared bus)

// Sequenza di inizializzazione hardware
void hw_reset_all_peripherals(void);
void hw_reset_gt911_for_address_0x14(void);
void hw_reset_gt911_for_address_0x5D(void);

#endif // HW_INIT_H
