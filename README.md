# Guition JC1060P470C BSP - Full Feature Demo

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.3-blue)](https://github.com/espressif/esp-idf)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-ESP32--P4-orange)](https://www.espressif.com/en/products/socs/esp32-p4)
[![Latest Release](https://img.shields.io/badge/release-v1.0.0--beta-brightgreen)](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/releases/tag/v1.0.0-beta)
[![Development](https://img.shields.io/badge/dev-v1.3.0--dev-yellow)](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo)

**Complete Board Support Package demonstration for the Guition JC1060P470C_I_W_Y development board featuring ESP32-P4.**

This project provides a comprehensive demonstration of all hardware capabilities of the Guition JC1060P470C board, featuring advanced peripheral management, reliable initialization sequences, optimized LVGL v9 graphics integration, and a deterministic three-phase bootstrap manager to handle complex SDMMC bus arbitration.

---

## ⚠️ Important Notices

### 🎨 LVGL v9 Integration Status (This Branch)

> [!NOTE]
> **Branch**: `feature/lvgl-v9-integration`
>
> This branch features **LVGL v9.2.2 integration** with optimized memory configuration.
>
> **Recent Fix (2026-03-04)**:
> - ✅ LVGL DSI configuration optimized (`avoid_tearing = false`)
> - ✅ Memory usage reduced: **~2.0 MB** (saved 800 KB vs previous config)
> - ✅ Compatible with `num_fbs = 1` (single hardware frame buffer)
> - ✅ Display 1024×600 MIPI DSI fully functional with touch input
> - 📄 See [docs/LVGL_DSI_CONFIGURATION.md](docs/LVGL_DSI_CONFIGURATION.md) for technical details

**What works on this branch:**
- ✅ LVGL v9.2.2 fully integrated with display and touch
- ✅ Optimized memory configuration (2.0 MB vs 2.8 MB)
- ✅ All I2C peripherals (Touch GT911, Audio ES8311, RTC RX8025T)
- ✅ WiFi (ESP-Hosted) fully functional on SDMMC Slot 1
- ⚠️ SD Card support disabled by default (see below)

**For LVGL-less configuration with SD Card working, use**:
- Branch: `main` or release `v1.0.0-beta`

---

### 💾 SD Card Support Status

> [!WARNING]
> **SD Card support is currently DISABLED by default** on this branch due to an unresolved SDMMC controller slot arbitration issue.
>
> **Known Issue:**
> - Error **0x108 (SDIO timeout)** occurs during WiFi→SD slot switch
> - ESP-Hosted triggers automatic host restart when detecting the error
> - Causes **boot loop** preventing system initialization
>
> **Recommendation:**
> - Use **WiFi-only configuration** for production (default)
> - SD Card support is **experimental** and suspended pending ESP-Hosted patch
> - Enable `CONFIG_BSP_ENABLE_SDCARD=y` in menuconfig **only if needed**

**What works:**
- ✅ WiFi (ESP-Hosted) fully functional on SDMMC Slot 1
- ✅ All I2C peripherals (Touch, Audio, RTC) fully functional  
- ✅ Display (MIPI-DSI) with LVGL v9 fully functional
- ⚠️ SD Card (SDMMC Slot 0) **disabled by default** due to slot switching issue

---

## 📋 Versions

### Latest Stable Release

**[v1.0.0-beta](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/releases/tag/v1.0.0-beta)** (2026-03-01)

- ✅ All 8 onboard peripherals fully functional (including SD Card)
- ✅ Three-phase bootstrap manager with deterministic initialization
- ✅ WiFi connection and RTC NTP synchronization
- ✅ Comprehensive documentation and troubleshooting guide
- ⚠️ Beta status: Production testing ongoing
- ❌ No LVGL integration (use `feature/lvgl-v9-integration` branch for LVGL)

**Installation:**
```bash
git clone https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo.git
cd guition-jc1060p470c-bsp-full-feature-demo
git checkout v1.0.0-beta
idf.py build flash monitor
```

### Development Version - LVGL Branch

**feature/lvgl-v9-integration** (this branch)

- ✅ LVGL v9.2.2 fully integrated
- ✅ Optimized memory configuration (avoid_tearing=false)
- ✅ Display 1024×600 with touch input
- ✅ WiFi (ESP-Hosted) fully functional
- ⚠️ **SD Card disabled by default** (slot arbitration issue)
- 📄 See [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md) for latest updates

**Installation:**
```bash
git clone https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo.git
cd guition-jc1060p470c-bsp-full-feature-demo
git checkout feature/lvgl-v9-integration
idf.py build flash monitor
```

### Development Version - Main Line

**v1.3.0-dev** (develop/v1.3.0 branch)

- 🚧 Active development with latest features (no LVGL)
- ✅ Fixed NTP sync with ESP-Hosted (callback-based detection)
- ✅ Enhanced NTP diagnostic tools (DNS, ping, detailed logging)
- ⚠️ **SD Card disabled by default** (slot arbitration issue)
- 🚧 May contain experimental features

**Installation:**
```bash
git clone https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo.git
cd guition-jc1060p470c-bsp-full-feature-demo
git checkout develop/v1.3.0
idf.py build flash monitor
```

**See [RELEASE_NOTES.md](RELEASE_NOTES.md) for complete version history.**

---

## 📋 Table of Contents

- [Important Notices](#️-important-notices)
  - [LVGL v9 Integration Status](#-lvgl-v9-integration-status-this-branch)
  - [SD Card Support Status](#-sd-card-support-status)
- [Versions](#-versions)
- [Features](#-features)
- [Hardware Support](#-hardware-support)
- [System Architecture](#-system-architecture)
- [Requirements](#-requirements)
- [Getting Started](#-getting-started)
- [Hardware Pinout](#-hardware-pinout)
- [Advanced Features](#-advanced-features)
- [Build Configuration](#️-build-configuration)
- [Troubleshooting](#-troubleshooting)
- [Documentation](#-documentation)
- [Roadmap](#-roadmap)
- [Contributing](#-contributing)
- [License](#-license)

---

## ✨ Features

### Hardware Support

- **🖥️ Display**: JD9165 4.7" 1024x600 MIPI DSI touchscreen with full graphics acceleration
- **🎨 LVGL v9**: Integrated graphics library with optimized memory config (this branch) - ✅ **WORKING**
- **👆 Touch Controller**: GT911 capacitive multi-touch (up to 5 points) with gesture support - ✅ **WORKING**
- **🔊 Audio Codec**: ES8311 I2S audio codec with integrated speaker amplifier control
- **⏰ Real-Time Clock**: RX8025T I2C RTC with battery backup and automatic NTP synchronization
- **💾 Storage**: SD card (SDMMC Slot 0) - ⚠️ **DISABLED BY DEFAULT** (see notice above)
- **📡 Connectivity**: WiFi 802.11b/g/n via ESP-Hosted on ESP32-C6 (SDMMC Slot 1) - ✅ **STABLE**
- **🔌 I2C Bus**: Fast-mode (400 kHz) with multiple peripheral support
- **💾 NVS Storage**: Non-Volatile Storage for persistent configuration

### Software Features

- **🎨 LVGL v9.2.2 Integration** (this branch): Memory-optimized configuration with DSI display support
- **⚡ Three-Phase Bootstrap Manager**: Deterministic initialization preventing SDMMC bus conflicts
- **🔄 Automatic Power Sequencing**: Hardware reset cycle on warm boot for clean initialization
- **🏛️ Feature Flags System**: Easy enable/disable of peripherals via Kconfig (menuconfig)
- **🐛 Debug Tracing**: Per-peripheral debug output control for detailed diagnostics
- **📊 System Monitoring**: Real-time boot timing analysis and hardware status tracking
- **🛡️ Error Handling**: Robust error recovery for all peripherals with detailed logging

### LVGL Configuration (This Branch)

**Memory-Optimized Setup**:
- Hardware frame buffer: `num_fbs = 1` (single buffer, 1.2 MB)
- LVGL draw buffers: 2 × (1024×200 pixels) in PSRAM (~800 KB)
- Total memory: **~2.0 MB** (saved 800 KB vs `avoid_tearing=true` config)
- DMA2D acceleration enabled
- Anti-tearing managed by LVGL (software)

**See [docs/LVGL_DSI_CONFIGURATION.md](docs/LVGL_DSI_CONFIGURATION.md) for complete configuration guide.**

### Known Issues

> [!CAUTION]
> **SD Card + WiFi Simultaneous Use**
>
> Enabling both SD Card and WiFi causes SDMMC slot arbitration conflict:
> - **Error 0x108 (SDIO timeout)** during slot switch (WiFi Slot 1 → SD Slot 0)
> - **ESP-Hosted auto-restart** triggers boot loop (unless disabled - see workaround below)
> - **Clean Switch Protocol** implemented but insufficient to prevent error
>
> **Current Status**: SD Card support **suspended** pending ESP-Hosted driver patch

---

## 🔧 Hardware Support

### Target Board

**Guition JC1060P470C_I_W_Y** - ESP32-P4 Development Board

| Component | Specification | Status (This Branch) |
|-----------|--------------|----------------------|
| **Main Processor** | ESP32-P4 @ 360 MHz | ✅ Stable |
| **WiFi Coprocessor** | ESP32-C6 | ✅ Stable (WiFi works) |
| **PSRAM** | 32 MB @ 200 MHz | ✅ Stable |
| **Flash** | 16 MB @ 40 MHz | ✅ Stable |
| **Display** | 4.7" 1024x600 MIPI DSI | ✅ Stable + LVGL v9 |
| **Touch** | GT911 capacitive multi-touch | ✅ Stable + LVGL input |
| **Audio** | I2S codec + amplifier | ✅ Stable |
| **RTC** | Battery-backed I2C RTC | ✅ Stable |
| **Storage** | MicroSD slot | ⚠️ **Disabled (see notice)** |

---

## 🏛️ System Architecture

### Bootstrap Manager Architecture

The Bootstrap Manager implements a **deterministic three-phase initialization** to prevent SDMMC bus conflicts between SD Card (Slot 0) and ESP-Hosted WiFi (Slot 1).

#### The Problem

Both peripherals share the same SDMMC controller. Simultaneous initialization causes:
- Bus contention and data corruption
- SD card mount failures (`0x107` errors)
- ESP-Hosted communication timeouts
- Unpredictable behavior on warm boot
- **NEW**: Error `0x108` (timeout) triggering ESP-Hosted auto-restart

#### The Solution (Partial)

Sequential initialization with **event-driven synchronization** across three phases:

### Three-Phase Initialization System

```
Phase A: Power Manager (BSP, Priority 24)
  ├── 1. Configure SD card power control (GPIO36)
  ├── 2. Force SD card power OFF (isolation)
  ├── 3. Wait 100ms for rail stabilization
  ├── 4. Power up SD card (GPIO36 HIGH)
  └── 5. Signal POWER_READY → Phase C

Phase C: WiFi Manager (Priority 23)
  ├── Wait for POWER_READY
  ├── 1. Initialize ESP-Hosted SDIO transport (SDMMC Slot 1)
  ├── 2. Wait 2000ms for SDIO link stabilization
  └── 3. Signal WIFI_READY → Phase B

Phase B: SD Manager (Priority 22) - ⚠️ DISABLED BY DEFAULT
  ├── Wait for WIFI_READY
  ├── 1. Attempt clean slot switch (pause ESP-Hosted transport)
  ├── 2. Deinitialize SDMMC Slot 1 controller
  ├── 3. Reinitialize for Slot 0 (SD Card)
  └── 4. Mount SD card filesystem
  
  ⚠️ KNOWN ISSUE:
      - Step 2 (controller deinit) triggers Error 0x108
      - ESP-Hosted detects error and restarts host (if CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE=y)
      - Causes boot loop
      - FIX SUSPENDED pending ESP-Hosted patch
```

### Current Workaround

**Disable SD Card by default** (`CONFIG_BSP_ENABLE_SDCARD=n`) to avoid slot arbitration:
- WiFi (Slot 1) works perfectly
- All I2C peripherals work perfectly
- Display with LVGL v9 works perfectly
- SD Card support disabled until driver fix available

**See [troubleshooting.md](troubleshooting.md) for complete technical details.**

---

## 📋 Requirements

### Software Requirements

- **ESP-IDF**: v5.5.3 or later
- **CMake**: 3.22 or later
- **Python**: 3.8 or later
- **Git**: For cloning repository and submodules

### Hardware Requirements

- **Guition JC1060P470C_I_W_Y** development board
- **USB-C cable** for programming and power
- **WiFi router** with 2.4GHz support (for WiFi features)
- **MicroSD card** (optional, experimental - see notice above)

### Optional Requirements

- **Speaker** (for audio testing)
- **CR2032 battery** (for RTC backup)

---

## 🚀 Getting Started

### Prerequisites

1. **Install ESP-IDF v5.5.3+**
   ```bash
   git clone -b v5.5.3 --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh esp32p4
   . ./export.sh
   ```

2. **Verify installation**
   ```bash
   idf.py --version
   # Expected: ESP-IDF v5.5.3 or later
   ```

### Quick Start (This Branch - LVGL Enabled)

1. **Clone this repository**
   ```bash
   git clone https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo.git
   cd guition-jc1060p470c-bsp-full-feature-demo
   git checkout feature/lvgl-v9-integration
   ```

2. **Set ESP-IDF target**
   ```bash
   idf.py set-target esp32p4
   ```

3. **Build the project**
   ```bash
   idf.py build
   ```

4. **Flash and monitor**
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor
   ```
   (Replace `/dev/ttyUSB0` with your serial port)

5. **Exit monitor**
   - Press `Ctrl+]` to exit
   - Press `Ctrl+T` then `Ctrl+R` to restart without reflashing

### Configuration

All peripherals can be enabled/disabled via **Kconfig** (menuconfig):

```bash
idf.py menuconfig
```

Navigate to: **Guition JC1060P470C Board Configuration → Hardware Peripherals**

| Peripheral | Config Option | Default | Notes |
|------------|--------------|---------|-------|
| **Display** | `BSP_ENABLE_DISPLAY` | ✅ ON | MIPI-DSI 1024x600 |
| **LVGL** | `BSP_ENABLE_LVGL` | ✅ ON | Graphics library v9.2.2 |
| **I2C Bus** | `BSP_ENABLE_I2C` | ✅ ON | Required for Touch/Audio/RTC |
| **Touch** | `BSP_ENABLE_TOUCH` | ✅ ON | GT911 capacitive + LVGL input |
| **Audio** | `BSP_ENABLE_AUDIO` | ✅ ON | ES8311 codec |
| **RTC** | `BSP_ENABLE_RTC` | ✅ ON | RX8025T battery-backed |
| **WiFi** | `BSP_ENABLE_WIFI` | ✅ ON | ESP-Hosted SDIO |
| **SD Card** | `BSP_ENABLE_SDCARD` | ❌ **OFF** | **Experimental** (see notice) |

> [!WARNING]
> Do NOT enable SD Card (`BSP_ENABLE_SDCARD`) unless you understand the slot arbitration issue and accept potential boot loops.

---

## 📊 System Status Overview

### ✅ Hardware Components (Working)

| Component | Status | Notes |
|-----------|--------|-------|
| **I2C Bus** | ✅ Stable | SDA=GPIO7, SCL=GPIO8 @ 400kHz |
| **ES8311 Audio** | ✅ Stable | I2C 0x18, PA control GPIO11 |
| **RX8025T RTC** | ✅ Stable | I2C 0x32, battery backup |
| **JD9165 Display** | ✅ Stable | MIPI-DSI 1024x600 + LVGL v9 |
| **GT911 Touch** | ✅ Stable | I2C 0x14, 5-point + LVGL input |
| **WiFi ESP-Hosted** | ✅ Stable | SDMMC Slot 1, ESP32-C6 |
| **Bootstrap Manager** | ✅ Stable | Three-phase init (WiFi-only) |
| **NVS Flash** | ✅ Stable | Non-volatile storage |
| **LVGL v9.2.2** | ✅ Stable | Optimized memory config |

### ⚠️ Experimental/Disabled Components

| Component | Status | Notes |
|-----------|--------|-------|
| **SD Card** | ⚠️ **Disabled** | Slot arbitration issue (Error 0x108) |

---

## 🔌 Hardware Pinout

### SDMMC Slot 1 (ESP-Hosted WiFi) - ✅ STABLE

| ESP32-P4 Pin | Function | Status |
|--------------|----------|--------|
| GPIO14-19 | SDIO Data/Cmd/Clk | ✅ Working |
| GPIO54 | C6_RESET | ✅ Working |
| GPIO52-53 | Handshake/DataReady | ✅ Working |

### SDMMC Slot 0 (SD Card) - ⚠️ DISABLED

| ESP32-P4 Pin | SD Card Pin | Status |
|--------------|-------------|--------|
| GPIO43 | CLK | ⚠️ Disabled |
| GPIO44 | CMD | ⚠️ Disabled |
| GPIO39-42 | D0-D3 | ⚠️ Disabled |
| GPIO36 | PWR_EN | ✅ Configured (BSP) |

### I2C Bus - ✅ STABLE

| Function | GPIO | Status |
|----------|------|--------|
| SDA | GPIO7 | ✅ Working |
| SCL | GPIO8 | ✅ Working |

**I2C Devices:**
- `0x14` - GT911 Touch Controller ✅
- `0x18` - ES8311 Audio Codec ✅
- `0x32` - RX8025T RTC ✅

---

## 🚀 Advanced Features

### LVGL Demo Applications (This Branch)

This branch includes LVGL v9 integration. You can test various LVGL demos:

**Available demos:**
- `lv_demo_widgets()` - UI component showcase
- `lv_demo_music()` - Music player UI
- `lv_demo_stress()` - Performance stress test
- Custom applications with touch input

**To run demos**, modify `main/main.c` and call the desired demo function.

### WiFi Connection Test

By default, the demo initializes WiFi without connecting. To enable full WiFi connection:

1. **Create WiFi credentials file:**
   ```bash
   cd main
   cp wifi_config.h.example wifi_config.h
   ```

2. **Edit credentials:**
   ```c
   // In main/wifi_config.h
   #define WIFI_SSID "YourWiFiSSID"
   #define WIFI_PASSWORD "YourWiFiPassword"
   ```

3. **Enable connection via menuconfig:**
   ```bash
   idf.py menuconfig
   ```
   Navigate to: **Guition Board Config → Application Features → Enable WiFi auto-connect**

4. **Rebuild and flash:**
   ```bash
   idf.py build flash monitor
   ```

**Expected Output:**
```
I (7770) GUITION_MAIN: === WiFi Connection Test ===
I (8829) GUITION_MAIN: ✓ WiFi connected!
I (8829) GUITION_MAIN:    IP: 192.168.188.88
I (8829) GUITION_MAIN:    RSSI: -67 dBm
```

### RTC NTP Synchronization

Synchronize RTC with Network Time Protocol (requires WiFi connection):

1. **Enable via menuconfig:**
   ```bash
   idf.py menuconfig
   ```
   Navigate to: **Guition Board Config → Application Features → Enable RTC NTP time sync**

2. **Build and flash:**
   ```bash
   idf.py build flash monitor
   ```

**Features:**
- NTP Server: pool.ntp.org
- Timezone: CET (UTC+1) with DST
- Callback-based success detection (fixes ESP-Hosted race condition)
- Typical sync time: 12-13 seconds

---

## ⚡ Build Configuration

| Parameter | Value | Notes |
|-----------|-------|-------|
| **ESP-IDF** | v5.5.3 | Framework version |
| **Target** | ESP32-P4 | Main chip |
| **CPU Frequency** | 360 MHz | Clock speed |
| **PSRAM** | 32 MB @ 200MHz | External RAM |
| **Flash** | 16 MB @ 40MHz | SPI flash |
| **SDMMC Slot 1** | 4-bit @ 40MHz | WiFi (STABLE) |
| **SDMMC Slot 0** | Disabled | SD Card (EXPERIMENTAL) |
| **LVGL** | v9.2.2 | Graphics library (this branch) |

---

## 🐛 Troubleshooting

### Common Issues

#### LVGL display not working

**Symptoms:**
```
E (1464) lcd.dsi: esp_lcd_dpi_panel_get_frame_buffer(409): invalid frame buffer number
E (1464) LVGL: lvgl_port_add_disp_priv(341): Get RGB buffers failed
```

**Solution**: Already fixed in this branch! See [docs/LVGL_DSI_CONFIGURATION.md](docs/LVGL_DSI_CONFIGURATION.md)

The fix sets `avoid_tearing = false` to work with `num_fbs = 1`, saving 800 KB of memory.

#### SD Card enabled causes boot loop

**Symptoms:**
```
E (8799) sdmmc_io: sdmmc_io_rw_extended: sdmmc_send_cmd returned 0x108
E (8799) H_SDIO_DRV: failed to read registers
I (8799) H_SDIO_DRV: Host is resetting itself
I (8799) os_wrapper_esp: Restarting host
```

**Root Cause:**

The error occurs because:
1. Bootstrap manager attempts to switch SDMMC controller from WiFi (Slot 1) to SD Card (Slot 0)
2. During `sdmmc_host_deinit()`, ESP-Hosted detects communication error 0x108 (SDIO timeout)
3. By default, ESP-Hosted is configured to restart the host when transport errors occur
4. This triggers `esp_restart()`, causing infinite boot loop

**Solution 1: Disable SD Card (Recommended)**

Disable SD Card in menuconfig:
```bash
idf.py menuconfig
# Navigate to: Guition Board Config → Hardware Peripherals
# Set "Enable SD Card" to [ ] (disabled)
idf.py build flash
```

**Solution 2: Disable ESP-Hosted Auto-Restart (Advanced)**

If you need to experiment with SD Card support, disable the auto-restart behavior:

```bash
idf.py menuconfig
# Navigate to: Component config → ESP-Hosted config
# Find "Restart transport on failure"
# Set to [ ] (disabled)
idf.py build flash
```

> [!CAUTION]
> **Critical Configuration for System Stability**
>
> The `CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE` option controls whether
> ESP-Hosted automatically restarts the host system when SDIO communication errors occur.
>
> **When ENABLED (default):**
> - Error 0x108 during SDMMC slot switching triggers automatic `esp_restart()`
> - Causes infinite boot loop when SD Card is enabled
> - Recommended for production WiFi-only configurations
>
> **When DISABLED:**
> - System continues operation despite SDIO errors
> - Allows experimentation with SD Card support
> - May leave ESP-Hosted in undefined state if errors occur
> - WiFi functionality may be degraded after error
>
> **This project REQUIRES this option DISABLED** when `CONFIG_BSP_ENABLE_SDCARD=y`

**Recommended Configuration for WiFi-Only (Stable):**
```bash
CONFIG_BSP_ENABLE_SDCARD=n  # SD Card disabled
CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE=n  # No auto-restart
CONFIG_ESP_HOSTED_SLAVE_RESET_ONLY_IF_NECESSARY=y  # Smart reset
```

**Why this configuration works:**
- SD Card disabled prevents slot arbitration errors
- No auto-restart prevents boot loops if errors occur
- Smart reset minimizes unnecessary ESP32-C6 resets
- System remains stable and WiFi works perfectly

#### WiFi connection timeout

**Checklist:**
1. Verify credentials in `main/wifi_config.h`
2. Ensure router broadcasts 2.4GHz (5GHz not supported)
3. Check signal strength
4. Verify `CONFIG_APP_ENABLE_WIFI_CONNECT=y` in menuconfig

#### GT911 Touch "clear bus failed" error

**Symptoms:**
```
E (6224) i2c.master: clear bus failed.
E (6224) GT911: touch_gt911_read_cfg(410): GT911 read error!
```

**Solution:** I2C bus scan must remain disabled (already default)

**See [troubleshooting.md](troubleshooting.md) for complete troubleshooting guide.**

---

## 📚 Documentation

### Technical Documentation (This Branch)

- **[docs/LVGL_DSI_CONFIGURATION.md](docs/LVGL_DSI_CONFIGURATION.md)** - ✨ **NEW (2026-03-04)**
  - Complete LVGL DSI configuration guide
  - `avoid_tearing` flag behavior and memory optimization
  - Configuration comparison and best practices
  
- **[docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md)** - Current project status and recent fixes

- **[docs/LVGL_V9_FRAME_BUFFER_FIX.md](docs/LVGL_V9_FRAME_BUFFER_FIX.md)** - Original frame buffer fix documentation

### General Documentation

- **[troubleshooting.md](troubleshooting.md)** - Complete troubleshooting guide
- **[SDMMC_ARBITER_README.md](SDMMC_ARBITER_README.md)** - SDMMC bus arbitration details
- **[I2C_MIPI_DSI_CONFLICT.md](I2C_MIPI_DSI_CONFLICT.md)** - I2C/MIPI DSI conflict resolution
- **[RELEASE_NOTES.md](RELEASE_NOTES.md)** - Version history

---

## 🗺️ Roadmap

### Current Status: LVGL v9 + WiFi Configuration Stable ✅

- ✅ LVGL v9.2.2 fully integrated with optimized memory
- ✅ WiFi (ESP-Hosted) fully functional
- ✅ All I2C peripherals working
- ✅ Display 1024×600 with touch input working
- ⚠️ SD Card support suspended (slot arbitration issue)

### Next Steps

- [ ] Test complex LVGL demos (widgets, music, stress)
- [ ] Audio integration with LVGL
- [ ] ESP-Hosted patch for 0x108 error handling
- [ ] SD Card + WiFi simultaneous support
- [ ] Example LVGL applications
- [ ] Power management integration

---

## 🤝 Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

---

## 📝 License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

---

**Project Status**: ✅ LVGL v9 + WiFi Stable | **Last Updated**: 2026-03-04
