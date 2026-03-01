# Troubleshooting Guide - ESP32-P4 + ESP-Hosted + SD Card

## Hardware Setup

- **MCU**: ESP32-P4 (Host)
- **Coprocessor**: ESP32-C6 (via ESP-Hosted SDIO)
- **Display**: JD9165 MIPI DSI 1024x600
- **Touch**: GT911 I2C (address 0x14)
- **Audio**: ES8311 I2C (address 0x18)
- **RTC**: RX8025T I2C (address 0x32)
- **Storage**: SD Card (SDMMC Slot 0)
- **WiFi**: ESP-Hosted (SDMMC Slot 1)

---

## Problem History & Solutions

### ❌ Problem 1: SD Card OCR Timeout (0x107) with ESP-Hosted Active

**Commit**: `0da9779` - "feat: add WiFi init with ENABLE_WIFI flag (test ESP-Hosted)"

**Symptoms**:
```
E (1512) sdmmc_common: sdmmc_init_ocr: send_op_cond (1) returned 0x107
E (1512) vfs_fat_sdmmc: sdmmc_card_init failed (0x107).
E (1520) GUITION_MAIN: Failed to mount SD card (0x107)
```

**Root Cause**:
- ESP-Hosted initializes SDMMC controller for Slot 1 (C6 communication) at boot
- SD Card on Slot 0 cannot initialize because controller is already claimed
- Both slots share the same SDMMC peripheral controller

**Boot Log**:
```
I (1101) H_SDIO_DRV: sdio_data_to_rx_buf_task started  ← ESP-Hosted init FIRST
I (1124) GUITION_MAIN: Initializing SD card (Slot 0 - forced)...
I (1376) GUITION_MAIN: SD Card power enabled via GPIO45 (waited 250ms)
I (1376) GUITION_MAIN: ESP-Hosted detected - using workaround
I (1376) GUITION_MAIN: Slot 0 initialized successfully
E (1512) sdmmc_common: sdmmc_init_ocr: send_op_cond (1) returned 0x107  ← FAIL
```

**WiFi worked**:
```
I (8859) GUITION_MAIN: ✓ WiFi scan successful - ESP-Hosted is working!
```

---

### ❌ Problem 2: Slot Deinit Causes Crash

**Commit**: `008dc74` - "fix: add sdmmc_host_deinit_slot(0) before SD init"

**Attempted Fix**:
- Called `sdmmc_host_deinit_slot(SDMMC_HOST_SLOT_0)` before reinit
- Goal: Reset slot state to allow SD card access

**Result**: **CRASH**

**Boot Log**:
```
I (1111) GUITION_MAIN: Initializing SD card (Slot 0 - forced)...
I (1118) GUITION_MAIN: Resetting SDMMC Slot 0 (ESP-Hosted workaround)...
I (1118) GUITION_MAIN: Slot 0 deinitialized successfully
I (1472) GUITION_MAIN: SD Card power enabled via GPIO45 (waited 250ms)
I (1472) GUITION_MAIN: ESP-Hosted detected - reinitializing slot 0
E (1473) GUITION_MAIN: Failed to reinit slot 0 (0x103)  ← ESP_ERR_INVALID_STATE

I (1483) wifi_hosted: Inizializzazione interfaccia Wi-Fi Hosted...
...
assert failed: xQueueSemaphoreTake queue.c:1709 (( pxQueue ))  ← CRASH!
Core  0 register dump:
MEPC    : 0x4ff08fa6  RA      : 0x4ff08b26  SP      : 0x4ff28910
```

**Root Cause**:
- `sdmmc_host_deinit_slot()` deallocates shared resources (semaphores, queues)
- ESP-Hosted on Slot 1 depends on these shared resources
- When WiFi tries to use SDIO, it finds destroyed semaphore → **crash**

**Lesson**: **NEVER call `deinit_slot()` when ESP-Hosted is active!**

---

### ✅ Solution: Reinit Slot WITHOUT Deinit

**Commit**: `1dd8fcb` - "fix: remove deinit_slot - only reinit slot 0 (preserve ESP-Hosted state)"

**Key Changes**:
1. Removed `sdmmc_host_deinit_slot(0)` call
2. Call `sdmmc_host_init_slot(SDMMC_HOST_SLOT_0, &slot_config)` directly
3. Override `host.init` and `host.deinit` with dummy functions

**Code**:
```c
#ifdef CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
    LOG_SD(TAG, "ESP-Hosted detected - initializing slot 0 (no deinit)");
    ret = sdmmc_host_init_slot(SDMMC_HOST_SLOT_0, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init slot 0 (0x%x)", ret);
        goto sd_failed;
    }
    LOG_SD(TAG, "Slot 0 initialized successfully");
    vTaskDelay(pdMS_TO_TICKS(100));
#endif

sdmmc_host_t host = SDMMC_HOST_DEFAULT();
host.slot = SDMMC_HOST_SLOT_0;

#ifdef CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
    host.init = &sdmmc_host_init_dummy;    // Skip controller init
    host.deinit = &sdmmc_host_deinit_dummy; // Keep controller active
#endif
```

**Successful Boot Log**:
```
I (1121) GUITION_MAIN: Initializing SD card (Slot 0 - forced)...
I (1373) GUITION_MAIN: SD Card power enabled via GPIO45 (waited 250ms)
I (1373) GUITION_MAIN: ESP-Hosted detected - initializing slot 0 (no deinit)
I (1375) GUITION_MAIN: Slot 0 initialized successfully
I (1479) GUITION_MAIN: Forced host.slot = SDMMC_HOST_SLOT_0
I (1479) GUITION_MAIN: Skipping sdmmc_host_init (controller already initialized by ESP-Hosted)
I (1530) GUITION_MAIN: ✓ SD card mounted successfully  ← SUCCESS!
I (1530) GUITION_MAIN: Card name: SU08G
I (1530) GUITION_MAIN: Capacity: 7580 MB
```

**WiFi Still Works**:
```
I (3076) sdio_wrapper: SDIO master: Slot 1, Data-Lines: 4-bit Freq(KHz)[40000 KHz]
I (3226) H_SDIO_DRV: Card init success, TRANSPORT_RX_ACTIVE
I (3327) transport: Identified slave [esp32c6]
I (8854) GUITION_MAIN: ✓ WiFi scan successful - ESP-Hosted is working!  ← SUCCESS!
```

---

### ❌ Problem 3: RTC Bus Configuration Error

**Commit**: `50b399a` - "feat: add second I2C bus (I2C_NUM_1) for RTC - GPIO10+12"

**Initial Assumption (WRONG)**:
- Assumed RTC was on separate I2C bus (GPIO12+10)
- Created I2C_NUM_1 for RTC
- RTC not found on separate bus

**Symptoms**:
```
========== I2C BUS 0 SCAN ==========
[0x14] ✓ GT911 Touch
[0x18] ✓ ES8311 Audio Codec
Total devices: 2

========== I2C BUS 1 SCAN ==========
Total devices: 0  ← RTC NOT FOUND!
```

**Root Cause (from schematic analysis)**:
- **ALL I2C devices share the SAME bus**: GPIO7 (SDA) + GPIO8 (SCL)
- Schematic shows GPIO7/8 signals split to multiple devices
- RTC, Audio, Touch are all on I2C_NUM_0
- No separate I2C bus exists!

**Solution**:
**Commit**: `139033a` + `73128f0` - "fix: remove I2C Bus 1 - RTC is on Bus 0 with Audio+Touch"

**Corrected Code**:
```c
// Single I2C Bus for ALL devices
#define I2C_MASTER_SDA_IO 7
#define I2C_MASTER_SCL_IO 8

// All devices on I2C_NUM_0:
// - 0x14: GT911 Touch
// - 0x18: ES8311 Audio
// - 0x32: RX8025T RTC
```

---

### ❌ Problem 4: RTC I2C Error 0x103 - Bus Reset Failed

**Commit**: `aa66203` + `0af9a98` - RTC driver integrated

**Symptoms**:
```
I (14313) GUITION_MAIN: 
========== RTC INITIALIZATION ==========
I (14319) RX8025T: Initializing RX8025T RTC...
I (14323) RX8025T: I2C Address: 0x32
I (14327) RX8025T: Clearing PON/VLF flags...
E (14382) i2c.master: clear bus failed.
E (14382) i2c.master: s_i2c_transaction_start(686): reset hardware failed
E (14382) RX8025T: Failed to clear flags (0x103)
E (14385) GUITION_MAIN: Failed to initialize RTC (0x103)
```

**I2C Scan Results**:
```
========== I2C BUS SCAN ==========
[0x14] ✓ GT911 Touch (INT=HIGH)
[0x18] ✓ ES8311 Audio Codec
Total devices: 2  ← RTC NOT RESPONDING!
```

**Root Cause Analysis**:
1. **RTC not detected in I2C scan** → Device not ACKing at 0x32
2. **I2C bus reset fails** when trying to write → Hardware issue or wrong address
3. **ESP_ERR_INVALID_STATE (0x103)** → I2C transaction cannot start

**Possible Causes**:
- ✅ RTC not populated on board (hardware missing)
- ⚠️ RTC in low-power mode (needs VBAT)
- ⚠️ Wrong I2C address (should verify with schematic)
- ⚠️ RTC requires specific wake-up sequence
- ⚠️ I2C timing issue (400kHz too fast?)

**Next Steps to Debug**:
1. **Check if RTC chip is physically present** (look for U9 on PCB)
2. **Try lower I2C speed** (100kHz instead of 400kHz)
3. **Probe with oscilloscope** (check SDA/SCL signals)
4. **Try alternative I2C addresses** (0x30, 0x31, 0x33)
5. **Read RTC without write** (try read-only operation first)

---

## Boot Logs History

### 🔴 Latest Boot Log - RTC I2C Error (2026-03-01 16:02)

**Date**: 2026-03-01 16:02:18 CET  
**Commit**: `aa66203` - RTC driver integrated  
**Status**: ❌ RTC initialization failed (0x103)

**Full Boot Log**:
```
ESP-ROM:esp32p4-eco2-20240710
Build:Jul 10 2024
rst:0x17 (CHIP_USB_UART_RESET),boot:0xc (SPI_FAST_FLASH_BOOT)

I (27) boot: ESP-IDF v5.5.3-dirty 2nd stage bootloader
I (28) boot: compile time Mar  1 2026 16:02:18
I (956) cpu_start: cpu freq: 360000000 Hz
I (958) app_init: Application information:
I (962) app_init: Project name:     sd_card
I (966) app_init: App version:      aa66203
I (970) app_init: Compile time:     Mar  1 2026 16:01:46

=== SD CARD INIT ===
I (1117) GUITION_MAIN: Initializing SD card (Slot 0 - forced)...
I (1369) GUITION_MAIN: SD Card power enabled via GPIO45 (waited 250ms)
I (1371) GUITION_MAIN: Slot 0 initialized successfully
I (1526) GUITION_MAIN: ✓ SD card mounted successfully
I (1526) GUITION_MAIN: Card name: SU08G
I (1526) GUITION_MAIN: Capacity: 7580 MB

=== WIFI INIT ===
I (1527) GUITION_MAIN: Initializing WiFi (ESP-Hosted via C6)...
I (3222) H_SDIO_DRV: Card init success, TRANSPORT_RX_ACTIVE
I (3323) transport: Identified slave [esp32c6]
I (8849) RPC_WRAP: ESP Event: StaScanDone
I (8850) GUITION_MAIN: ✓ WiFi scan successful - ESP-Hosted is working!

=== I2C BUS SCAN ===
I (9683) GUITION_MAIN: 
========== I2C BUS SCAN (Audio + Touch + RTC) ==========

[0x14] ✓ GT911 Touch (INT=HIGH)
[0x18] ✓ ES8311 Audio Codec

Total devices: 2

I (14289) I2C_SCAN: Expected devices:
I (14292) I2C_SCAN:   ✓ 0x14 = GT911 Touch (board config)
I (14298) I2C_SCAN:   ✓ 0x18 = ES8311 Audio Codec
I (14302) I2C_SCAN:   ? 0x32 = RX8025T RTC (needs init)  ← NOT FOUND!

=== RTC INIT (FAILED) ===
I (14313) GUITION_MAIN: 
========== RTC INITIALIZATION ==========
I (14319) RX8025T: Initializing RX8025T RTC...
I (14323) RX8025T: I2C Address: 0x32
I (14327) RX8025T: Clearing PON/VLF flags...
E (14382) i2c.master: clear bus failed.               ← I2C ERROR!
E (14382) i2c.master: s_i2c_transaction_start(686): reset hardware failed
E (14382) RX8025T: Failed to clear flags (0x103)      ← ESP_ERR_INVALID_STATE
E (14385) GUITION_MAIN: Failed to initialize RTC (0x103)
W (14390) GUITION_MAIN: RTC might not be populated on board or on different I2C address
I (14398) GUITION_MAIN: ========== RTC INIT COMPLETE ==========

I (14408) GUITION_MAIN: === System ready ===
```

**Summary**:
- ✅ SD Card: Working (7580 MB)
- ✅ WiFi: Working (scan successful)
- ✅ I2C Bus: Working (GT911 + ES8311 found)
- ❌ RTC: Not detected at 0x32
- ❌ RTC Init: I2C transaction failed (0x103)

---

## Working Configuration Summary

### Init Sequence (CRITICAL ORDER)

```
1. NVS Flash Init
2. SD Card Init:
   - Power ON via GPIO45 (250ms delay)
   - sdmmc_host_init_slot(SLOT_0) - NO DEINIT!
   - esp_vfs_fat_sdmmc_mount() with dummy init/deinit
3. WiFi/ESP-Hosted Init:
   - init_wifi() via ESP-Hosted
   - WiFi scan test
4. Hardware Reset (GPIO toggles for I2C devices)
5. I2C Bus Init (single bus GPIO7+8)
6. I2C Scan (detect all devices)
7. RTC Init (RX8025T driver) ← FAILING
8. Display Init (MIPI DSI - disabled)
9. Touch Init (GT911 - disabled)
```

### Feature Flags (feature_flags.h)

```c
#define ENABLE_SD_CARD 1  // ✅ Working
#define ENABLE_WIFI 1     // ✅ Working  
#define ENABLE_I2C 1      // ✅ Working (single bus)
#define ENABLE_RTC 1      // ❌ Failed (not detected)
#define ENABLE_DISPLAY 0  // Not tested yet
#define ENABLE_TOUCH 0    // Not tested yet
```

### Key Code Patterns

**Dummy Functions for ESP-Hosted Compatibility**:
```c
#ifdef CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE
static esp_err_t sdmmc_host_init_dummy(void) 
{ 
    LOG_SD(TAG, "Skipping sdmmc_host_init (controller already initialized by ESP-Hosted)");
    return ESP_OK; 
}

static esp_err_t sdmmc_host_deinit_dummy(void) 
{ 
    LOG_SD(TAG, "Skipping sdmmc_host_deinit (keep controller active for ESP-Hosted)");
    return ESP_OK; 
}
#endif
```

**I2C Bus Configuration**:
```c
// Single I2C bus for ALL devices
i2c_master_bus_config_t i2c_bus_config = {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_NUM_0,
    .scl_io_num = 8,
    .sda_io_num = 7,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};
```

---

## Common Errors & Solutions

### Error: `ESP_ERR_TIMEOUT (0x107)` - OCR Timeout

**Symptoms**: SD card init times out during OCR (Operating Conditions Register) negotiation

**Causes**:
- SDMMC controller not initialized for Slot 0
- GPIO not configured correctly
- SD card power not stable
- Timing issues (insufficient delays)

**Solutions**:
1. Call `sdmmc_host_init_slot(SLOT_0)` explicitly
2. Add 250ms delay after GPIO power enable
3. Add 100ms delay after slot init
4. Force `host.slot = SDMMC_HOST_SLOT_0`

### Error: `ESP_ERR_INVALID_STATE (0x103)` - I2C Transaction Failed

**Symptoms**: I2C master cannot start transaction, bus reset fails

**Causes**:
- Device not responding (missing or powered off)
- Wrong I2C address
- Bus timing issue
- Device in low-power mode

**Solutions**:
1. Verify device is physically present on PCB
2. Check power supply to device (VBAT for RTC)
3. Try lower I2C speed (100kHz)
4. Probe with oscilloscope to verify signals
5. Try read-only operations first

### Error: `assert failed: xQueueSemaphoreTake` - FreeRTOS Crash

**Symptoms**: Crash in FreeRTOS queue operations

**Causes**:
- Shared resource (semaphore/queue) deallocated by `deinit_slot`
- ESP-Hosted tries to use destroyed semaphore

**Solutions**:
- **NEVER call `sdmmc_host_deinit_slot()` with ESP-Hosted active**
- Use dummy deinit function to skip cleanup

---

## Complete Hardware Pin Mapping

### Controllo Sistema

| Funzione | Pin ESP32-P4 | Note Hardware |
|----------|--------------|---------------|
| Reset ESP32-C6 | GPIO 54 | Collegato a C6_CHIP_PU; attivo LOW |
| Power SD Card | GPIO 36/45 | Controlla il MOSFET Q1 per alimentare TF_VCC |

### Display (MIPI DSI)

| Funzione | Pin ESP32-P4 | Note Hardware |
|----------|--------------|---------------|
| Backlight PWM | GPIO 23 | Segnale LCD_PWM per la retroilluminazione |
| Reset Display | GPIO 27 | Segnale LCD_RST mappato sul connettore FPC |
| DSI Clock | GPIO 34, 35 | D-PHY Clock pair (differential) |
| DSI Data Lane 0 | GPIO 36, 37 | D-PHY Data pair 0 (differential) |
| DSI Data Lane 1 | GPIO 38, 39 | D-PHY Data pair 1 (differential) |

### Bus I2C (Controller singolo - I2C_NUM_0)

| Funzione | Pin ESP32-P4 | Note Hardware |
|----------|--------------|---------------|
| SDA (Dati) | GPIO 7 | Condiviso tra TUTTI i device I2C |
| SCL (Clock) | GPIO 8 | Richiede resistenze di pull-up esterne (2.2kΩ tipiche) |

**Devices su I2C_NUM_0 (GPIO7+8)**:
- `0x14`: GT911 Touch Controller ✅ Working
- `0x18`: ES8311 Audio Codec ✅ Working
- `0x32`: RX8025T RTC (U9) ❌ Not detected

### Touch Screen (GT911)

| Funzione | Pin ESP32-P4 | Note Hardware |
|----------|--------------|---------------|
| Interrupt (INT) | GPIO 21 | Pin TOUCH_INT per gestire gli eventi di tocco |
| Reset (RST) | GPIO 22 | Pin TOUCH_RST usato per definire l'indirizzo I2C |

**Indirizzo I2C:**
- INT=HIGH durante reset → Indirizzo 0x14 ✅ Configured
- INT=LOW durante reset → Indirizzo 0x5D (default)

### SD Card (Slot 0)

| Funzione | Pin ESP32-P4 | Note Hardware |
|----------|--------------|---------------|
| CLK | GPIO 43 | Clock SDMMC |
| CMD | GPIO 44 | Command line |
| Data D0 | GPIO 39 | Data bit 0 |
| Data D1 | GPIO 40 | Data bit 1 |
| Data D2 | GPIO 41 | Data bit 2 |
| Data D3 | GPIO 42 | Data bit 3 |
| Power Enable | GPIO 36/45 | Alimentazione controllata |

**Note**: Bus dati a 4-bit per alta velocità

### ESP-Hosted WiFi (Slot 1 - SDIO)

| Funzione | Pin ESP32-P4 | Note Hardware |
|----------|--------------|---------------|
| CLK | GPIO 18 | Clock SDIO |
| CMD | GPIO 19 | Command line |
| Data D0 | GPIO 14 | Data bit 0 |
| Data D1 | GPIO 15 | Data bit 1 |
| Data D2 | GPIO 16 | Data bit 2 |
| Data D3 | GPIO 17 | Data bit 3 |
| C6 Reset | GPIO 54 | Reset ESP32-C6 coprocessor |

**Note**: Collegamento interno SDIO tra P4 e C6

### Debug & Flash

| Funzione | Pin ESP32-P4 | Note Hardware |
|----------|--------------|---------------|
| Console UART TX | GPIO 37 | UART0 TX per monitor seriale |
| Console UART RX | GPIO 38 | UART0 RX per monitor seriale |
| Flash SPI | GPIO 27-33 | Pin dedicati per la memoria Flash esterna |
| Boot Mode | GPIO 0 | Modalità boot (HIGH=normal, LOW=download) |

---

## Testing Checklist

- [x] SD Card Mount (7580 MB SanDisk)
- [x] WiFi/ESP-Hosted Init
- [x] WiFi Scan (networks detected)
- [x] I2C Bus Init (single bus GPIO7+8)
- [x] I2C Scan (GT911 + ES8311 found)
- [x] RTC Driver Integration
- [ ] **RTC Detection on I2C (FAILING - not at 0x32)**
- [ ] **RTC Initialization (FAILING - 0x103 error)**
- [ ] RTC Read Time
- [ ] RTC Write Time
- [ ] Display Init (JD9165)
- [ ] Display Test Pattern (RGB)
- [ ] Touch Init (GT911)
- [ ] Touch Input Test

---

## References

- [ESP-IDF SDMMC Host Driver](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/sdmmc_host.html)
- [ESP-IDF I2C Master Driver](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/i2c.html)
- [ESP-Hosted Documentation](https://github.com/espressif/esp-hosted)
- [ESP32-P4 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-p4_datasheet_en.pdf)
- [GT911 Touch Controller Datasheet](https://www.displayfuture.com/Display/datasheet/controller/GT911.pdf)
- [RX8025T RTC Datasheet](https://www.uugear.com/doc/datasheet/RX8025T.pdf)

---

## Git Commit History

```bash
# View full commit history
git log --oneline --graph --all

# Key commits:
0da9779 - feat: add WiFi init with ENABLE_WIFI flag (test ESP-Hosted)
008dc74 - fix: add sdmmc_host_deinit_slot(0) before SD init ❌ CAUSED CRASH
1dd8fcb - fix: remove deinit_slot - only reinit slot 0 ✅ WORKING
7162db3 - test: enable I2C + scan (no halt) with SD+WiFi working
b5e40f6 - fix: remove halt after I2C scan
50b399a - feat: add second I2C bus for RTC ❌ WRONG ASSUMPTION
139033a - fix: remove I2C Bus 1 - RTC is on Bus 0 ✅ CORRECTED
73128f0 - docs: update hw_init - all I2C devices on single bus
9fe3b67 - feat: add RTC enable flag to feature_flags
aa66203 - feat: add RTC initialization and test ❌ I2C ERROR 0x103
0af9a98 - docs: fix I2C bus config + add boot log section
```

---

## License

This troubleshooting guide is provided as-is for educational purposes.
