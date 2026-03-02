# Guition JC1060P470C BSP - Full Feature Demo

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.3-blue)](https://github.com/espressif/esp-idf)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-ESP32--P4-orange)](https://www.espressif.com/en/products/socs/esp32-p4)

**Complete Board Support Package demonstration for the Guition JC1060P470C_I_W_Y development board featuring ESP32-P4.**

This project provides a comprehensive demonstration of all hardware capabilities of the Guition JC1060P470C board, featuring advanced peripheral management, reliable initialization sequences, and a deterministic three-phase bootstrap manager to handle complex SDMMC bus arbitration.

---

## 📑 Table of Contents

- [Features](#-features)
- [Hardware Support](#-hardware-support)
- [System Architecture](#-system-architecture)
  - [Bootstrap Manager](#bootstrap-manager-architecture)
  - [Three-Phase Initialization](#three-phase-initialization-system)
- [Requirements](#-requirements)
- [Getting Started](#-getting-started)
  - [Prerequisites](#prerequisites)
  - [Quick Start](#quick-start)
  - [Configuration](#configuration)
- [Hardware Pinout](#-hardware-pinout)
- [Advanced Features](#-advanced-features)
  - [WiFi Connection](#wifi-connection-test)
  - [RTC NTP Sync](#rtc-ntp-synchronization)
- [Build Configuration](#%EF%B8%8F-build-configuration)
- [Development Workflow](#-development-workflow)
- [Troubleshooting](#-troubleshooting)
- [Documentation](#-documentation)
- [Contributing](#-contributing)
- [License](#-license)

---

## ✨ Features

### Hardware Support

- **🖥️ Display**: JD9165 4.7" 1024x600 MIPI DSI touchscreen with full graphics acceleration
- **👆 Touch Controller**: GT911 capacitive multi-touch (up to 5 points) with gesture support
- **🔊 Audio Codec**: ES8311 I2S audio codec with integrated speaker amplifier control
- **⏰ Real-Time Clock**: RX8025T I2C RTC with battery backup and automatic NTP synchronization
- **💾 Storage**: SD card (SDMMC Slot 0) with FAT32 filesystem and 4-bit bus @ 20MHz
- **📡 Connectivity**: WiFi 802.11b/g/n via ESP-Hosted on ESP32-C6 (SDMMC Slot 1)
- **🔌 I2C Bus**: Fast-mode (400 kHz) with multiple peripheral support
- **💾 NVS Storage**: Non-Volatile Storage for persistent configuration

### Software Features

- **⚡ Three-Phase Bootstrap Manager**: Deterministic initialization preventing SDMMC bus conflicts
- **🔄 Automatic Power Sequencing**: Hardware reset cycle on warm boot for clean initialization
- **🎛️ Feature Flags System**: Easy enable/disable of peripherals via compile-time flags
- **🐛 Debug Tracing**: Per-peripheral debug output control for detailed diagnostics
- **📊 System Monitoring**: Real-time boot timing analysis and hardware status tracking
- **🛡️ Error Handling**: Robust error recovery for all peripherals with detailed logging

### Workarounds & Fixes

> [!IMPORTANT]
> This project includes a workaround for [ESP-IDF Issue #16233](https://github.com/espressif/esp-idf/issues/16233) that prevents SD card and ESP-Hosted from working together when both use SDMMC.

---

## 🔧 Hardware Support

### Target Board

**Guition JC1060P470C_I_W_Y** - ESP32-P4 Development Board

| Component | Specification | Notes |
|-----------|--------------|-------|
| **Main Processor** | ESP32-P4 @ 360 MHz | Dual-core RISC-V |
| **WiFi Coprocessor** | ESP32-C6 | Via ESP-Hosted SDIO |
| **PSRAM** | 32 MB @ 200 MHz | Octal SPI |
| **Flash** | 16 MB @ 40 MHz | Quad SPI |
| **Display** | 4.7" 1024x600 | MIPI DSI interface |
| **Touch** | Capacitive multi-touch | I2C interface |
| **Audio** | I2S codec + amplifier | Stereo output |
| **RTC** | Battery-backed I2C RTC | Power loss protection |
| **Storage** | MicroSD slot | SDMMC 4-bit interface |

### Supported Hosts

| Platform | Status | Notes |
|----------|--------|-------|
| **ESP32-P4** | ✅ Fully Supported | Primary target |
| **ESP32-S3** | ⚠️ Partial | SDMMC arbitration may differ |

---

## 🏗️ System Architecture

### Bootstrap Manager Architecture

The Bootstrap Manager implements a **deterministic three-phase initialization** to prevent SDMMC bus conflicts between SD Card (Slot 0) and ESP-Hosted WiFi (Slot 1).

#### The Problem

Both peripherals share the same SDMMC controller. Simultaneous initialization causes:
- Bus contention and data corruption
- SD card mount failures (`0x107` errors)
- ESP-Hosted communication timeouts
- Unpredictable behavior on warm boot

#### The Solution

Sequential initialization with **event-driven synchronization** across three phases:

### Three-Phase Initialization System

```
Phase A: Power Manager (Priority 24)
  ├── 1. Force GPIO isolation (C6 in reset, SD powered down)
  ├── 2. Wait 100ms for rail stabilization
  ├── 3. Power up SD card (GPIO36 HIGH)
  ├── 4. Release C6 from reset (GPIO54 HIGH)
  └── 5. Signal POWER_READY → Phase B

Phase B: WiFi Manager (Priority 23)
  ├── Wait for POWER_READY
  ├── 1. Wait for C6 firmware ready signal (GPIO6, 5s timeout)
  ├── 2. Initialize ESP-Hosted SDIO transport (SDMMC Slot 1)
  └── 3. Signal HOSTED_READY → Phase C

Phase C: SD Manager (Priority 22)
  ├── Wait for HOSTED_READY
  ├── 1. Enable pull-ups on SDMMC pins (GPIO39-44)
  ├── 2. Mount SD card filesystem (SDMMC Slot 0)
  └── 3. Signal SD_READY
```

#### Why This Works

1. **Phase A** ensures power domains are stable before any communication
2. **Phase B** initializes ESP-Hosted and claims exclusive SDMMC controller access
3. **Phase C** safely mounts SD card after ESP-Hosted is fully operational

#### Boot Timing Analysis

| Event | Timestamp | Duration | Description |
|-------|-----------|----------|-------------|
| **Bootstrap Start** | T+1.87s | - | Three-phase init begins |
| **Warm Boot Detection** | T+1.89s | 20ms | Detects reset reason |
| **Hard Reset Cycle** | T+1.90s | 500ms | Forces complete power-down |
| **Phase A** | T+2.43s | 180ms | Power Manager execution |
| **Phase B** | T+2.61s | 5.06s | WiFi Manager (incl. 5s timeout) |
| **Phase C** | T+7.67s | 360ms | SD Manager + mount |
| **Bootstrap Complete** | T+8.03s | **6.16s total** | All systems operational |

> [!NOTE]
> The GPIO6 handshake timeout (5 seconds) is expected behavior. ESP-Hosted proceeds successfully after timeout without affecting functionality.

#### Warm Boot Hard Reset

On warm boot (software reset, hardware button, or unknown reset), the Bootstrap Manager performs an **automatic hard reset cycle**:

```
W (1891) BOOTSTRAP: Warm boot detected (reset reason: 3)
W (1896) BOOTSTRAP: Warm boot detected, performing hard reset...
W (1902) BOOTSTRAP: === HARD RESET CYCLE ===
W (1906) BOOTSTRAP: Forcing complete power-down to clear hardware state...
W (1913) BOOTSTRAP:   GPIO54 (C6_CHIP_PU) → LOW
W (1917) BOOTSTRAP:   GPIO36 (SD_POWER_EN) → LOW
W (1922) BOOTSTRAP:   Waiting 500ms for capacitor discharge...
W (2427) BOOTSTRAP: Hard reset complete, ready for clean init
```

**Purpose**: Ensures ESP32-C6 and SD card start from a known clean state, preventing residual hardware state issues.

**See [troubleshooting.md](troubleshooting.md#bootstrap-manager-timing-and-behavior) for complete timing analysis.**

---

## 📋 Requirements

### Software Requirements

- **ESP-IDF**: v5.5.3 or later
- **CMake**: 3.22 or later
- **Python**: 3.8 or later
- **Git**: For cloning repository and submodules

### Hardware Requirements

- **Guition JC1060P470C_I_W_Y** development board
- **MicroSD card** (FAT32 formatted, 32GB max recommended)
- **USB-C cable** for programming and power
- **WiFi router** with 2.4GHz support (for WiFi features)

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

### Quick Start

1. **Clone this repository**
   ```bash
   git clone https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo.git
   cd guition-jc1060p470c-bsp-full-feature-demo
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

All peripherals can be enabled/disabled via feature flags in `main/feature_flags.h`:

```c
// Core Peripherals
#define ENABLE_I2C          1  // I2C bus (required for RTC, Audio, Touch)
#define ENABLE_DISPLAY      1  // JD9165 MIPI DSI display
#define ENABLE_TOUCH        1  // GT911 touch controller
#define ENABLE_AUDIO        1  // ES8311 audio codec
#define ENABLE_RTC          1  // RX8025T RTC
#define ENABLE_SD_CARD      1  // SD card filesystem
#define ENABLE_WIFI         1  // ESP-Hosted WiFi
#define ENABLE_NVS          1  // Non-volatile storage

// Advanced Features (disabled by default)
#define ENABLE_WIFI_CONNECT 0  // WiFi connection test
#define ENABLE_RTC_NTP_SYNC 0  // NTP time synchronization
#define ENABLE_DISPLAY_TEST 0  // RGB test pattern
#define ENABLE_TOUCH_TEST   0  // Continuous touch reading

// Debug Output
#define DEBUG_I2C           1  // I2C transaction logging
#define DEBUG_DISPLAY       1  // Display initialization
#define DEBUG_TOUCH         1  // Touch events
#define DEBUG_AUDIO         1  // Audio codec status
#define DEBUG_RTC           1  // RTC operations
#define DEBUG_SD_CARD       1  // SD card operations
#define DEBUG_WIFI          1  // WiFi/ESP-Hosted status
```

---

## 📊 System Status Overview

### ✅ Hardware Components (All Working)

| Component | Status | I2C Address | GPIO Pins | Feature Flag | Debug Flag |
|-----------|--------|-------------|-----------|--------------|------------|
| **I2C Bus** | ✅ Active | - | SDA=7, SCL=8 | `ENABLE_I2C=1` | `DEBUG_I2C=1` |
| **ES8311 Audio** | ✅ Active | 0x18 | PA_CTRL=11 | `ENABLE_AUDIO=1` | `DEBUG_AUDIO=1` |
| **RX8025T RTC** | ✅ Active | 0x32 | - | `ENABLE_RTC=1` | `DEBUG_RTC=1` |
| **JD9165 Display** | ✅ Active | - | MIPI DSI (45-52) | `ENABLE_DISPLAY=1` | `DEBUG_DISPLAY=1` |
| **GT911 Touch** | ✅ Active | 0x14 | RST=21, INT=22 | `ENABLE_TOUCH=1` | `DEBUG_TOUCH=1` |
| **SD Card** | ✅ Active | - | Slot 0 (39-44), PWR=45 | `ENABLE_SD_CARD=1` | `DEBUG_SD_CARD=1` |
| **WiFi ESP-Hosted** | ✅ Active | - | Slot 1 (14-19), RST=54 | `ENABLE_WIFI=1` | `DEBUG_WIFI=1` |
| **Bootstrap Manager** | ✅ Active | - | C6_RST=54, SD_PWR=36, C6_READY=6 | - | - |
| **NVS Flash** | ✅ Active | - | - | `ENABLE_NVS=1` | `DEBUG_NVS=0` |

### 🧪 Advanced Features and Tests

| Feature | Status | Flag | Requirements | Description |
|---------|--------|------|--------------|-------------|
| **WiFi Connection Test** | ✅ Available | `ENABLE_WIFI_CONNECT=1` | `wifi_config.h` | Connect to WiFi and display IP/RSSI |
| **RTC Read/Write Test** | ✅ Active | `ENABLE_RTC_TEST=1` | - | Display current RTC time |
| **RTC NTP Sync** | ⚙️ Available | `ENABLE_RTC_NTP_SYNC=0` | WiFi connected | Sync RTC with NTP server |
| **RTC Hardware Test** | ⚙️ Available | `ENABLE_RTC_HW_TEST=0` | - | Advanced RTC diagnostics |
| **Display RGB Test** | ⚙️ Available | `ENABLE_DISPLAY_TEST=0` | - | RGB test pattern |
| **Touch Input Test** | ⚙️ Available | `ENABLE_TOUCH_TEST=0` | - | Continuous touch reading |
| **I2C Bus Scan** | ❌ Disabled | `ENABLE_I2C_SCAN=0` | - | **DO NOT ENABLE** (interferes with GT911) |

### 📋 Expected Boot Log Output

| Initialization Stage | Tag | Expected Output | Time |
|---------------------|-----|-----------------|------|
| **Boot Info** | `app_init` | App version, compile time, ESP-IDF | ~1.0s |
| **I2C Bus** | `GUITION_MAIN` | ✓ I2C bus ready (SDA=GPIO7, SCL=GPIO8) | ~1.1s |
| **ES8311 Audio** | `ES8311` | ✓ ES8311 initialized (Chip ID: 0x83) | ~1.3s |
| **RTC** | `RX8025T` | ✓ RTC initialized, Current time | ~1.4s |
| **Display** | `JD9165` | Display initialized (1024x600) | ~1.7s |
| **Touch** | `GT911` | ✓ GT911 initialized (1024x600) | ~1.7s |
| **Bootstrap Start** | `BOOTSTRAP` | Three-phase init begins | ~1.9s |
| **Hard Reset** | `BOOTSTRAP` | 500ms power-down cycle (warm boot) | ~1.9-2.4s |
| **Phase A** | `BOOTSTRAP` | Power Manager (GPIO isolation + power-on) | ~2.4-2.6s |
| **Phase B** | `BOOTSTRAP` | WiFi Manager (ESP-Hosted init) | ~2.6-7.7s |
| **Phase C** | `BOOTSTRAP` | SD Manager (SD card mount) | ~7.7-8.0s |
| **SD Card Ready** | `GUITION_MAIN` | ✓ SD card mounted, Capacity | ~8.0s |
| **WiFi Init** | `wifi_hosted` | ✓ WiFi initialized (ESP-Hosted) | ~4.4s |
| **WiFi Connect** | `GUITION_MAIN` | ✓ WiFi connected! IP, RSSI | ~10.9s |

---

## 🔌 Hardware Pinout

### SDMMC Slot 0 (SD Card)

> [!NOTE]
> On ESP32-P4, SDMMC Slot 0 GPIO pins are fixed and cannot be customized.

| ESP32-P4 Pin | SD Card Pin | Notes |
|--------------|-------------|-------|
| GPIO43 | CLK | 10k pullup |
| GPIO44 | CMD | 10k pullup |
| GPIO39 | D0 | 10k pullup |
| GPIO40 | D1 | 10k pullup (4-bit mode only) |
| GPIO41 | D2 | 10k pullup (4-bit mode only) |
| GPIO42 | D3 | 10k pullup required even in 1-bit mode |
| GPIO36 | PWR_EN | SD card power control (active HIGH) |

### SDMMC Slot 1 (ESP-Hosted WiFi)

| ESP32-P4 Pin | Function | Notes |
|--------------|----------|-------|
| GPIO14-19 | SDIO Data | 4-bit SDIO interface |
| GPIO54 | C6_RESET | ESP32-C6 CHIP_PU (active HIGH) |
| GPIO6 | INT/READY | Dual-purpose: interrupt + ready signal |

### I2C Bus

| Function | GPIO Pin | Speed |
|----------|----------|-------|
| SDA | GPIO7 | 400 kHz |
| SCL | GPIO8 | 400 kHz |

**I2C Devices:**
- `0x14` - GT911 Touch Controller
- `0x18` - ES8311 Audio Codec
- `0x32` - RX8025T RTC

### MIPI DSI (Display)

| Function | GPIO Pins | Notes |
|----------|-----------|-------|
| DSI Data/Clock | GPIO45-52 | Fixed MIPI DSI interface |

### Other Peripherals

| Peripheral | GPIO Pin | Function |
|------------|----------|----------|
| Touch Reset | GPIO21 | GT911 hardware reset |
| Touch Interrupt | GPIO22 | GT911 touch event interrupt |
| Audio PA Control | GPIO11 | Speaker amplifier enable |

---

## 🚀 Advanced Features

### WiFi Connection Test

By default, the demo performs a WiFi scan. To enable full WiFi connection:

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

3. **Enable connection test:**
   ```c
   // In main/feature_flags.h
   #define ENABLE_WIFI_CONNECT 1  // Enable WiFi connection
   ```

4. **Rebuild and flash:**
   ```bash
   idf.py build flash monitor
   ```

**Expected Output:**
```
I (4429) GUITION_MAIN: ✓ WiFi initialized (ESP-Hosted via C6)
I (6429) GUITION_MAIN: === WiFi Connection Test ===
I (6429) GUITION_MAIN: Connecting to: YourWiFiSSID
I (8500) GUITION_MAIN: ✓ WiFi connected!
I (8500) GUITION_MAIN:    IP Address: 192.168.1.100
I (8501) GUITION_MAIN:    Netmask:    255.255.255.0
I (8502) GUITION_MAIN:    Gateway:    192.168.1.1
I (8503) GUITION_MAIN:    RSSI: -45 dBm
```

> [!NOTE]
> - `wifi_config.h` is gitignored for security
- ESP32-C6 supports only 2.4GHz WiFi (802.11b/g/n)
- 5GHz networks will not be visible

### RTC NTP Synchronization

Synchronize RTC with Network Time Protocol server (requires WiFi connection):

1. **Enable WiFi connection** (see above)

2. **Enable RTC NTP sync:**
   ```c
   // In main/feature_flags.h
   #define ENABLE_RTC 1            // Enable RTC
   #define ENABLE_RTC_NTP_SYNC 1   // Enable NTP sync
   #define ENABLE_WIFI_CONNECT 1   // Required for NTP
   ```

3. **Build and flash:**
   ```bash
   idf.py build flash monitor
   ```

**Test Workflow:**
1. Read current RTC time
2. Reset RTC to default (2000-01-01 00:00:00)
3. Synchronize with NTP server (pool.ntp.org)
4. Update RTC with synchronized time

**Features:**
- **NTP Server**: pool.ntp.org (public pool)
- **Timezone**: CET (UTC+1) with automatic DST
- **Timeout**: 10 seconds
- **Verification**: Readback confirmation

**Use Cases:**
- Initial RTC setup on first boot
- Periodic time synchronization
- Recovery from RTC battery failure
- Development/testing time sync

---

## ⚙️ Build Configuration

| Parameter | Value | Description |
|-----------|-------|-------------|
| **ESP-IDF** | v5.5.3-dirty | Framework version |
| **Target** | ESP32-P4 | Main chip |
| **CPU Frequency** | 360 MHz | Clock speed |
| **PSRAM** | 32 MB @ 200MHz | External RAM |
| **Flash** | 16 MB @ 40MHz QIO | SPI flash |
| **I2C Frequency** | 400 kHz | Fast-mode I2C |
| **SDMMC Slot 0** | 4-bit @ 20MHz | SD card |
| **SDMMC Slot 1** | 4-bit @ 40MHz | ESP-Hosted WiFi |
| **Optimization** | `-Os` | Size optimization |
| **Log Level** | INFO | Default verbosity |

### Configuration Files

| File | Purpose | Git Tracked | Notes |
|------|---------|-------------|-------|
| `main/feature_flags.h` | Enable/disable peripherals | ✅ Yes | Edit to configure features |
| `main/wifi_config.h` | WiFi credentials | ❌ No (.gitignore) | Copy from `.example` |
| `sdkconfig.defaults` | Build defaults | ✅ Yes | ESP-IDF configuration |
| `CMakeLists.txt` | Build system | ✅ Yes | Project name and dependencies |

---

## 💻 Development Workflow

### Recommended Reset Methods

| Reset Method | Reliability | SD Card | WiFi | Bootstrap | Recommended For |
|--------------|-------------|---------|------|-----------|----------------|
| **IDF Monitor Restart** | ⭐⭐⭐⭐⭐ | ✅ OK | ✅ OK | ✅ Clean | ✅ **Development** |
| **`idf.py monitor`** | ⭐⭐⭐⭐⭐ | ✅ OK | ✅ OK | ✅ Clean | ✅ **Development** |
| **Power Cycle (5s)** | ⭐⭐⭐⭐⭐ | ✅ OK | ✅ OK | ✅ Clean | ✅ **Production** |
| **Hardware Button** | ⭐⭐⭐ | ✅ OK | ✅ OK | ⚙️ Hard reset cycle | ⚠️ Works (warm boot) |
| **USB Disconnect** | ⭐⭐ | ⚠️ May fail | ⚠️ May timeout | ⚙️ Hard reset cycle | ❌ Less reliable |

### Best Practices

1. **Use IDF Monitor restart** (`Ctrl+T` then `Ctrl+R`) for development
2. **Full power cycle** for production testing
3. **Hardware button** triggers automatic hard reset (expected behavior)
4. **USB disconnect** may cause initialization issues (avoid if possible)

**See [troubleshooting.md](troubleshooting.md) for detailed reset behavior analysis.**

### Debugging Tips

- Enable per-peripheral debug flags in `feature_flags.h`
- Use `idf.py monitor` with filters: `idf.py monitor -p /dev/ttyUSB0 --print_filter="*:I GUITION_MAIN:D"`
- Check bootstrap timing in logs (look for `BOOTSTRAP` tag)
- Verify I2C devices are responding (check I2C addresses in logs)

---

## 🐛 Troubleshooting

### Common Issues

#### SD Card fails to initialize (`0x107` error)

**Symptoms:**
```
E (xxxx) sdmmc_cmd: sdmmc_init_sd_scr: send_scr (1) returned 0x107
```

**Common Causes:**
- Hardware button reset (use IDF monitor restart instead)
- USB disconnect/reconnect (power cycle for 5+ seconds)
- Inconsistent hardware state after soft reset

**Solutions:**
1. Use `Ctrl+T` + `Ctrl+R` in IDF monitor
2. Full power cycle (disconnect USB for 5+ seconds)
3. Check SD card is properly seated
4. Try different SD card (FAT32 formatted)

**See [troubleshooting.md](troubleshooting.md#system-reset-behavior-and-initialization-reliability) for complete analysis.**

#### Bootstrap timeout

**Symptoms:**
```
E (2617) BOOTSTRAP: Bootstrap timeout!
E (2635) BOOTSTRAP:   Phase B (WiFi Hosted) did not complete
```

**Note:** GPIO6 handshake timeout (5s) is **expected behavior**. ESP-Hosted proceeds successfully despite timeout message. Both WiFi and SD card work correctly.

**See [troubleshooting.md](troubleshooting.md#bootstrap-manager-timing-and-behavior) for timing details.**

#### GT911 Touch "clear bus failed" error

**Symptoms:**
```
E (6224) i2c.master: clear bus failed.
E (6224) GT911: touch_gt911_read_cfg(410): GT911 read error!
```

**Solution:** Disable I2C bus scan:
```c
// In main/feature_flags.h
#define ENABLE_I2C_SCAN 0  // Must be disabled
```

**Reason:** I2C scanning interferes with GT911 hardware reset sequence.

**See [troubleshooting.md](troubleshooting.md#gt911-touch-controller-initialization-issues) for detailed explanation.**

#### WiFi connection timeout

**Symptoms:** WiFi fails to connect or times out

**Checklist:**
1. Verify credentials in `main/wifi_config.h` are correct
2. Ensure router broadcasts 2.4GHz (ESP32-C6 doesn't support 5GHz)
3. Check WiFi signal strength (router proximity)
4. Verify GPIO6 interrupt configuration
5. Check firewall settings

#### NTP synchronization fails

**Symptoms:** NTP sync timeout or failure

**Checklist:**
1. Verify WiFi is connected (check IP address in logs)
2. Test internet connectivity
3. Check firewall allows UDP port 123 (NTP)
4. Try alternative NTP server (edit `rtc_ntp_sync.c`)
5. Increase timeout in `sync_time_from_ntp()` call

### Important Notes

⚠️ **I2C Scan Must Be Disabled**
- `ENABLE_I2C_SCAN=0` is required
- Scanning interferes with GT911 touch controller
- Causes "clear bus failed" errors

⚠️ **Single lwIP Reference Required**
- CMakeLists.txt must have only ONE `lwip` in REQUIRES
- Duplicate causes WiFi instability and SD card errors
- Fixed in commit `bb2168c` (2026-03-01)

⚠️ **RTC NTP Sync Requires WiFi**
- Enable `ENABLE_WIFI_CONNECT=1` first
- Verify WiFi connection succeeds
- Then enable `ENABLE_RTC_NTP_SYNC=1`

---

## 📚 Documentation

### Additional Resources

- **[troubleshooting.md](troubleshooting.md)** - Complete troubleshooting guide with:
  - Bootstrap Manager timing analysis
  - Reset behavior comparison
  - SD card `0x107` error root causes
  - I2C device initialization best practices
  - WiFi/ESP-Hosted debugging
  - Complete system boot logs
  - Hardware diagnostic procedures

- **[SDMMC_ARBITER_README.md](SDMMC_ARBITER_README.md)** - SDMMC bus arbitration details
- **[I2C_MIPI_DSI_CONFLICT.md](I2C_MIPI_DSI_CONFLICT.md)** - I2C and MIPI DSI conflict resolution
- **[RELEASE_NOTES.md](RELEASE_NOTES.md)** - Version history and changes
- **[CONTRIBUTING.md](CONTRIBUTING.md)** - Contribution guidelines

### Example Output

A Wi-Fi scan is performed before and after accessing the SD card, demonstrating that both SDMMC slots work as expected:

```
I (2103) transport: Received INIT event from ESP32 peripheral
I (2113) transport: Identified slave [esp32c6]
I (2123) transport: Features supported are:
I (2123) transport:      * WLAN
I (2133) transport:        - HCI over SDIO
I (2133) transport:        - BLE only

I (2643) example: Initializing SD card
I (2743) example: Mounting filesystem
I (2953) example: Filesystem mounted
I (2953) example: Doing Wi-Fi Scan
I (6203) example: Total APs scanned = 11

Name: SD16G
Type: SDHC
Speed: 40.00 MHz (limit: 40.00 MHz)
Size: 14868MB

I (6213) example: Opening file /sdcard/hello.txt
I (6303) example: File written
I (6333) example: Read from file: 'Hello SD16G!'
I (6363) example: Card unmounted
I (6363) example: Doing another Wi-Fi Scan
I (8883) example: Total APs scanned = 11

Done
```

---

## 🤝 Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

### Development Roadmap

Planned improvements (Phase 2+ refactoring):

1. **Component Architecture**: Separate BSP into reusable ESP-IDF component
2. **Driver Isolation**: Split hardware drivers from BSP logic
3. **Public API**: Centralized BSP initialization and configuration API
4. **LVGL Integration**: Graphics library support for display
5. **Audio Playback**: I2S audio streaming implementation
6. **Touch Gestures**: Advanced touch gesture recognition
7. **Power Management**: Deep sleep and power optimization

---

## 📄 License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

---

## 🙏 Acknowledgments

- Originally adapted from [ESP-IDF SD Card example](https://github.com/espressif/esp-idf/tree/master/examples/storage/sd_card/sdmmc)
- ESP-Hosted firmware by Espressif Systems
- Guition for the JC1060P470C development board

---

**Project Status**: ✅ Active Development | **Last Updated**: 2026-03-02
