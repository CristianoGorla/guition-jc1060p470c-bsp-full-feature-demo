#ifndef HW_INIT_H
#define HW_INIT_H

#include "driver/gpio.h"
#include "esp_err.h"

// Pin definitions from schematic
#define PIN_TOUCHINT        GPIO_NUM_21
#define PIN_TOUCHRST        GPIO_NUM_22
#define PIN_I2C_SDA         GPIO_NUM_7
#define PIN_I2C_SCL         GPIO_NUM_8

// Sequenza di inizializzazione hardware
void hw_reset_all_peripherals(void);
void hw_reset_gt911_for_address_0x14(void);
void hw_reset_gt911_for_address_0x5D(void);

#endif // HW_INIT_H
