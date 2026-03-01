# SDMMC Arbiter: WiFi/SD Card Multiplexing

## Problem Statement

The ESP32-P4 has a **hardware constraint**: the SDMMC controller cannot operate in both SDIO mode (for ESP-Hosted WiFi) and SD card host mode simultaneously on the same slot.

**Hardware Configuration:**
- **SDMMC Slot 0** (GPIO39-44): Shared between WiFi (SDIO) and SD card
- **ESP32-C6 WiFi**: Communicates via SDIO protocol on Slot 0
- **SD card**: Requires Slot 0 configured for SD card host mode

**Without arbiter:**
```
Phase B: WiFi init → SDMMC configured for SDIO
Phase C: SD mount  → TIMEOUT (0x107) because bus is in SDIO mode
```

## Solution: SDMMC Arbiter

The arbiter implements **software multiplexing** with mutual exclusion:

```
┌─────────────────────────────────────────┐
│         SDMMC Arbiter (Mutex)           │
├─────────────────┬───────────────────────┤
│  WiFi Mode      │   SD Card Mode        │
│  (SDIO slave)   │   (SD host)           │
├─────────────────┼───────────────────────┤
│ ESP-Hosted init │ SD card mount         │
│ WiFi operations │ File I/O operations   │
│ Network traffic │ Read/write files      │
└─────────────────┴───────────────────────┘
         ↓                   ↓
    SDMMC Slot 0 (GPIO39-44)
```

### Key Features

1. **Mutual Exclusion**: FreeRTOS mutex ensures only one mode is active
2. **Automatic Switching**: Safely deinitializes current mode before switching
3. **NVS Persistence**: Saves preferred mode across reboots
4. **Thread-Safe**: Multiple tasks can request mode changes
5. **Clean Lifecycle**: Proper init/deinit for both WiFi and SD subsystems

## Architecture

### Bootstrap Integration

**New 2-Phase Boot:**
```
Phase A: Power Management
  ↓
Phase B: WiFi Initialization (via arbiter)
  ↓
Bootstrap Complete → WiFi Ready
  ↓
SD Card: On-demand via arbiter API
```

**Previous 3-Phase Boot (broken):**
```
Phase A → Phase B (WiFi) → Phase C (SD mount) ❌ CONFLICT
```

### Component Files

- **`sdmmc_arbiter.h/c`**: Core arbiter implementation
- **`bootstrap_manager.c`**: Integrated with arbiter (Phase B)
- **`esp_hosted_wifi.c`**: Added `wifi_hosted_deinit_transport()`
- **`sd_card_manager.c`**: Added `sd_card_unmount_safe()`

## API Usage

### 1. Initialization (Done by Bootstrap)

```c
esp_err_t ret = sdmmc_arbiter_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Arbiter init failed");
}
```

### 2. Request WiFi Mode

```c
// Request WiFi mode (deinitializes SD if active)
esp_err_t ret = sdmmc_arbiter_request_wifi(5000);  // 5s timeout
if (ret == ESP_OK) {
    // WiFi mode granted, ESP-Hosted is active
    // SDMMC Slot 0 configured for SDIO
    init_wifi();  // Start WiFi operations
    wifi_connect("SSID", "password");
}
```

### 3. Request SD Card Mode

```c
// Request SD card mode (deinitializes WiFi if active)
sdmmc_card_t *card = NULL;
esp_err_t ret = sdmmc_arbiter_request_sd_card(5000, &card);
if (ret == ESP_OK) {
    // SD mode granted, card mounted
    // SDMMC Slot 0 reconfigured for SD card host
    FILE *f = fopen("/sdcard/test.txt", "r");
    // ... file operations ...
    fclose(f);
}
```

### 4. Release Mode (Optional)

```c
// Release WiFi mode (allows SD card requests)
sdmmc_arbiter_release_wifi();

// Release SD card mode (allows WiFi requests)
sdmmc_arbiter_release_sd_card();
```

### 5. Check Current Mode

```c
sdmmc_bus_mode_t mode = sdmmc_arbiter_get_mode();
if (mode == SDMMC_MODE_WIFI) {
    ESP_LOGI(TAG, "WiFi mode active");
} else if (mode == SDMMC_MODE_SD_CARD) {
    ESP_LOGI(TAG, "SD card mode active");
}
```

## Example Workflow

### Scenario: Boot with WiFi, then access SD card

```c
void app_main(void)
{
    // Bootstrap initializes arbiter and requests WiFi mode
    bootstrap_manager_t bootstrap;
    bootstrap_manager_init(&bootstrap);
    bootstrap_manager_wait(&bootstrap, 30000);
    
    // WiFi is ready, connect to network
    init_wifi();
    wifi_connect("MyNetwork", "password123");
    wait_for_ip();
    
    // Download file from server
    http_download("http://example.com/data.bin", "/tmp/data.bin");
    
    // Switch to SD card mode to save file
    sdmmc_card_t *card = NULL;
    esp_err_t ret = sdmmc_arbiter_request_sd_card(5000, &card);
    if (ret == ESP_OK) {
        // Copy downloaded file to SD card
        FILE *src = fopen("/tmp/data.bin", "rb");
        FILE *dst = fopen("/sdcard/data.bin", "wb");
        // ... copy loop ...
        fclose(src);
        fclose(dst);
        
        ESP_LOGI(TAG, "File saved to SD card");
    }
    
    // Switch back to WiFi mode for further network operations
    ret = sdmmc_arbiter_request_wifi(5000);
    if (ret == ESP_OK) {
        // WiFi available again
        http_post("http://example.com/status", "Download complete");
    }
}
```

## NVS Persistence

The arbiter saves the current mode to NVS:

```c
// Save current mode for next boot
sdmmc_arbiter_save_mode();

// Load saved mode (called automatically by arbiter_init)
sdmmc_bus_mode_t saved_mode;
sdmmc_arbiter_load_mode(&saved_mode);
```

**NVS Storage:**
- Namespace: `"sdmmc_arbiter"`
- Key: `"mode"`
- Value: `0` (NONE), `1` (WiFi), `2` (SD card)

## Bootstrap Manager Changes

### Old Behavior (3-Phase, Broken)

```c
Phase A: Power → POWER_READY
Phase B: WiFi  → HOSTED_READY  (SDMMC in SDIO mode)
Phase C: SD    → SD_READY       ❌ TIMEOUT (bus conflict)
```

### New Behavior (2-Phase, Arbiter)

```c
Phase A: Power          → POWER_READY
Phase B: WiFi (arbiter) → HOSTED_READY  (WiFi mode granted)
Bootstrap complete      → WiFi operational

Later: Request SD via sdmmc_arbiter_request_sd_card()
```

### Getting SD Card Handle

```c
bootstrap_manager_t bootstrap;
bootstrap_manager_init(&bootstrap);
bootstrap_manager_wait(&bootstrap, 30000);

// This internally calls sdmmc_arbiter_request_sd_card()
sdmmc_card_t *card = bootstrap_manager_get_sd_card(&bootstrap);
if (card) {
    ESP_LOGI(TAG, "SD card capacity: %llu MB",
             ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024));
}
```

## Performance Considerations

**Mode Switching Time:**
- WiFi → SD: ~5.1s (WiFi deinit + bus reconfiguration + SD mount)
- SD → WiFi: ~5.1s (SD unmount + bus reconfiguration + WiFi init + SDIO link stabilization)

**Recommendation:**
- Minimize mode switches (batch operations)
- If frequent SD access needed, consider staying in SD mode
- Use NVS persistence to boot into preferred mode

## Debugging

Enable arbiter logs:

```c
esp_log_level_set("SDMMC_ARBITER", ESP_LOG_DEBUG);
```

**Common Issues:**

1. **Timeout on mode request**: Another task holds the mutex
   - Solution: Increase timeout or release mode in other task

2. **"Already in X mode"**: Redundant request (harmless)
   - Solution: Check current mode before requesting

3. **Mount fails after switch**: Bus not properly deinitialized
   - Solution: Check deinit functions in WiFi/SD managers

## License

Unlicense (Public Domain)

Copyright (c) 2026 Cristiano Gorla
