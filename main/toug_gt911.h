#ifndef __TOUCH_GT911_H__
#define __TOUCH_GT911_H__

#include "driver/i2c_master.h"

#define TOUCH_I2C_SCL          8   // Confermato da test I2C_MON
#define TOUCH_I2C_SDA          7   // Confermato da test I2C_MON
#define TOUCH_GT911_RST        22  // Da schema Guition P4 [1]
#define TOUCH_GT911_INT        21  // Da schema Guition P4 [1]
#define TOUCH_GT911_ADDR       0x14 // Indirizzo rilevato nel log

void init_gt911_touch(i2c_master_bus_handle_t bus_handle);

#endif