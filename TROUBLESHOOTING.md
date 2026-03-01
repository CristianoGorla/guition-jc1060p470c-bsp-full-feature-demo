# Troubleshooting Guide - ESP32-P4 + ESP-Hosted + SD Card

## Hardware Setup

- **MCU**: ESP32-P4 (Host)
- **Coprocessor**: ESP32-C6 (via ESP-Hosted SDIO)
- **Display**: JD9165 MIPI DSI 1024x600
- **Touch**: GT911 I2C
- **Audio**: ES8311 I2C
- **RTC**: PCF8563 I2C
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
5. I2C Bus Init (GPIO 7+8)
6. Display Init (MIPI DSI)
7. Touch Init (GT911 via I2C)
```

### Feature Flags (feature_flags.h)

```c
#define ENABLE_SD_CARD 1  // ✅ Working
#define ENABLE_WIFI 1     // ✅ Working
#define ENABLE_I2C 1      // ✅ Working
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

**Slot Config for SD Card**:
```c
sdmmc_slot_config_t slot_config = {
    .clk = CONFIG_EXAMPLE_PIN_CLK,
    .cmd = CONFIG_EXAMPLE_PIN_CMD,
    .d0 = CONFIG_EXAMPLE_PIN_D0,
    .d1 = CONFIG_EXAMPLE_PIN_D1,
    .d2 = CONFIG_EXAMPLE_PIN_D2,
    .d3 = CONFIG_EXAMPLE_PIN_D3,
    .cd = SDMMC_SLOT_NO_CD,
    .wp = SDMMC_SLOT_NO_WP,
    .width = 4,
    .flags = 0,
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

### Error: `ESP_ERR_INVALID_STATE (0x103)` - Slot Init Fails

**Symptoms**: `sdmmc_host_init_slot()` returns 0x103

**Causes**:
- Controller in inconsistent state after deinit
- Trying to init already initialized slot without proper deinit

**Solutions**:
- Remove `sdmmc_host_deinit_slot()` call
- Let ESP-Hosted keep controller initialized
- Only reconfigure slot GPIO/settings

### Error: `assert failed: xQueueSemaphoreTake` - FreeRTOS Crash

**Symptoms**: Crash in FreeRTOS queue operations

**Causes**:
- Shared resource (semaphore/queue) deallocated by `deinit_slot`
- ESP-Hosted tries to use destroyed semaphore

**Solutions**:
- **NEVER call `sdmmc_host_deinit_slot()` with ESP-Hosted active**
- Use dummy deinit function to skip cleanup

---

## Hardware Pin Mapping

### SDMMC Slot 0 (SD Card)
```
CLK  = GPIO43
CMD  = GPIO44
D0   = GPIO39
D1   = GPIO40
D2   = GPIO41
D3   = GPIO42
PWR  = GPIO45 (power enable)
```

### SDMMC Slot 1 (ESP-Hosted - C6)
```
CLK  = GPIO18
CMD  = GPIO19
D0   = GPIO14
D1   = GPIO15
D2   = GPIO16
D3   = GPIO17
RST  = GPIO54 (C6 reset)
```

### I2C Bus (I2C_NUM_0)
```
SDA = GPIO7
SCL = GPIO8

Devices:
- 0x18: ES8311 (audio codec)
- 0x51: PCF8563 (RTC)
- 0x5D: GT911 (touch controller)
```

### MIPI DSI Display
```
Panel: JD9165
Resolution: 1024x600
Interface: MIPI DSI 4-lane
```

---

## Testing Checklist

- [x] SD Card Mount (7580 MB SanDisk)
- [x] WiFi/ESP-Hosted Init
- [x] WiFi Scan (networks detected)
- [x] I2C Bus Init
- [ ] I2C Device Scan (GT911/ES8311/RTC)
- [ ] Display Init (JD9165)
- [ ] Display Test Pattern (RGB)
- [ ] Touch Init (GT911)
- [ ] Touch Input Test

---

## References

- [ESP-IDF SDMMC Host Driver](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/sdmmc_host.html)
- [ESP-Hosted Documentation](https://github.com/espressif/esp-hosted)
- [ESP32-P4 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-p4_datasheet_en.pdf)

---

## Git Commit History

```bash
# View full commit history
git log --oneline --graph --all

# Key commits:
0da9779 - feat: add WiFi init with ENABLE_WIFI flag (test ESP-Hosted)
008dc74 - fix: add sdmmc_host_deinit_slot(0) before SD init (reset slot after ESP-Hosted)
1dd8fcb - fix: remove deinit_slot - only reinit slot 0 (preserve ESP-Hosted state) ✅ WORKING
7162db3 - test: enable I2C + scan (no halt) with SD+WiFi working
b5e40f6 - fix: remove halt after I2C scan - system continues after scan
```

---

## License

This troubleshooting guide is provided as-is for educational purposes.
