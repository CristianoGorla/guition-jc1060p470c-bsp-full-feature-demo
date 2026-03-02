# Guition JC1060P470C BSP - Full Feature Demo

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.3-blue)](https://github.com/espressif/esp-idf)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-ESP32--P4-orange)](https://www.espressif.com/en/products/socs/esp32-p4)
[![Latest Release](https://img.shields.io/badge/release-v1.0.0--beta-brightgreen)](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/releases/tag/v1.0.0-beta)
[![Development](https://img.shields.io/badge/dev-v1.3.0--dev-yellow)](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo)

**Complete Board Support Package demonstration for the Guition JC1060P470C_I_W_Y development board featuring ESP32-P4.**

This project provides a comprehensive demonstration of all hardware capabilities of the Guition JC1060P470C board, featuring advanced peripheral management, reliable initialization sequences, and a deterministic three-phase bootstrap manager to handle complex SDMMC bus arbitration.

---

## 📋 Versions

### Latest Stable Release

**[v1.0.0-beta](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/releases/tag/v1.0.0-beta)** (2026-03-01)

- ✅ All 8 onboard peripherals fully functional
- ✅ Three-phase bootstrap manager with deterministic initialization
- ✅ WiFi connection and RTC NTP synchronization
- ✅ Comprehensive documentation and troubleshooting guide
- ⚠️ Beta status: Production testing ongoing

**Installation:**
```bash
git clone https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo.git
cd guition-jc1060p470c-bsp-full-feature-demo
git checkout v1.0.0-beta
idf.py build flash monitor
```

### Development Version

**v1.3.0-dev** (develop/v1.3.0 branch)

- 🚧 Active development with latest features
- ✅ Fixed NTP sync with ESP-Hosted (callback-based detection)
- ✅ Enhanced NTP diagnostic tools (DNS, ping, detailed logging)
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

- [Versions](#-versions)
- [Features](#-features)
- [Hardware Support](#-hardware-support)
- [System Architecture](#-system-architecture)
  - [Bootstrap Manager](#bootstrap-manager-architecture)
  - [Three-Phase Initialization](#three-phase-initialization-system)
  - [BSP Minimal Architecture](#bsp-minimal-architecture)
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
- [Roadmap](#-roadmap)
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
- **🏛️ Feature Flags System**: Easy enable/disable of peripherals via compile-time flags
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

## 🏛️ System Architecture

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
Phase A: Power Manager (BSP, Priority 24)
  ├── 1. Configure SD card power control (GPIO36)
  ├── 2. Force SD card power OFF (isolation)
  ├── 3. Wait 100ms for rail stabilization
  ├── 4. Power up SD card (GPIO36 HIGH)
  └── 5. Signal POWER_READY → Phase C
  
  NOTE: C6 reset (GPIO54) and SDIO signals (GPIO18) are NOT managed by BSP.
        These are exclusively owned by ESP-Hosted and SDMMC drivers.

Phase C: WiFi Manager (Priority 23)
  ├── Wait for POWER_READY
  ├── 1. Initialize ESP-Hosted SDIO transport (SDMMC Slot 1)
  │   └── ESP-Hosted driver resets C6 via GPIO54 internally
  ├── 2. Wait 2000ms for SDIO link stabilization
  └── 3. Signal WIFI_READY → Phase B
  
  NOTE: esp_wifi_init() initializes SDMMC controller for Slot 1.
        C6 is reset automatically during this process.
        SDMMC host controller remains active after this phase.

Phase B: SD Manager (Priority 22)
  ├── Wait for WIFI_READY
  ├── 1. Enable pull-ups on SDMMC Slot 0 pins (GPIO39-44)
  ├── 2. Mount SD card filesystem using "dummy init"
  │   └── Skips sdmmc_host_init() (already done by WiFi)
  └── 3. Signal SD_READY
  
  NOTE: Uses "dummy init" because SDMMC host controller
        is already initialized and active from Phase C.
```

### BSP Minimal Architecture

**Key Principle: Driver Ownership**

The BSP follows a **minimal intervention** philosophy:

| GPIO | Owner | BSP Touches It? | Notes |
|------|-------|-----------------|-------|
| **GPIO36** | BSP | ✅ Yes | SD card power enable (only thing BSP manages) |
| **GPIO54** | ESP-Hosted | ❌ No | C6 reset (driver manages during esp_wifi_init) |
| **GPIO18** | SDMMC Driver | ❌ No | SDIO CLK for Slot 1 (cannot be forced) |
| **GPIO14-17,19** | SDMMC Driver | ❌ No | SDIO data/cmd lines for Slot 1 |
| **GPIO39-44** | SDMMC Driver | ❌ No | SD card data/cmd/clk for Slot 0 |

**Why This Matters:**

1. **C6 Reset Behavior:**
   - C6 **resets automatically** on every P4 boot
   - ESP-Hosted driver handles GPIO54 during `esp_wifi_init()`
   - BSP never touches GPIO54 - would cause conflicts

2. **SDMMC Host Controller:**
   - **Never resets** between P4 software reboots
   - WiFi init (Phase C) leaves it in active state
   - SD mount (Phase B) uses "dummy init" to avoid re-init
   - Attempting to re-initialize causes `0x107` errors

3. **GPIO18 (SDIO CLK):**
   - Shared between P4 SDIO Slot 1 and C6 IO9 (strapping pin)
   - Cannot be forced HIGH as "strapping guard" - breaks SDIO protocol
   - Hardware pull-up/down must handle C6 boot mode selection

**BSP Responsibilities:**
- ✅ SD card power control (GPIO36)
- ✅ Power sequencing delays
- ✅ Hard reset detection and handling
- ❌ **NOT** C6 reset (ESP-Hosted owns it)
- ❌ **NOT** SDIO signals (SDMMC driver owns them)
- ❌ **NOT** strapping manipulation (hardware handles it)

#### Why This Works

1. **Phase A** ensures SD card power is stable before any communication
2. **Phase C** initializes ESP-Hosted and claims SDMMC controller (C6 resets automatically)
3. **Phase B** safely mounts SD card without re-initializing active controller
4. **Driver Ownership** prevents GPIO conflicts and double-initialization

#### Boot Timing Analysis

| Event | Timestamp | Duration | Description |
|-------|-----------|----------|-------------|
| **Bootstrap Start** | T+1.41s | - | Three-phase init begins |
| **Phase A** | T+1.41s | 180ms | Power Manager (SD power only) |
| **Phase C** | T+2.09s | 5.38s | WiFi Manager (SDMMC init + C6 reset + link stabilization) |
| **Phase B** | T+7.47s | 360ms | SD Manager (dummy init + mount) |
| **Bootstrap Complete** | T+7.77s | **6.36s total** | All systems operational |
| **WiFi Connect** | T+7.77s | 1.03s | Connect + DHCP |
| **System Ready** | T+8.83s | **7.42s total** | WiFi connected with IP |

> [!NOTE]
> SDMMC controller initialization happens ONCE during Phase C (WiFi init) and persists.
> Phase B (SD mount) skips controller init to avoid conflicts.

#### Warm Boot Hard Reset

On warm boot (software reset, hardware button, or unknown reset), the BSP performs a hard reset cycle **only for SD card**:

```
I (1411) BSP: [RESET] USB-UART reset (IDF monitor) - no hard reset needed
I (1411) BSP: [RESET] ESP-Hosted will manage C6 reset during init_wifi()
I (1412) BSP: [PHASE A] Configuring SD card power control...
I (1412) BSP: [PHASE A]   GPIO36 (SD_POWER_EN) → LOW (SD unpowered)
I (1412) BSP: [PHASE A] NOTE: GPIO54 (C6) and GPIO18 (SDIO CLK) managed by drivers
```

**Purpose**: Ensures SD card starts from a clean state. C6 reset is handled by ESP-Hosted driver automatically.

**See [troubleshooting.md](troubleshooting.md#bsp-minimal-architecture-and-driver-ownership) for complete technical details.**

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
| **SD Card** | ✅ Active | - | Slot 0 (39-44), PWR=36 | `ENABLE_SD_CARD=1` | `DEBUG_SD_CARD=1` |
| **WiFi ESP-Hosted** | ✅ Active | - | Slot 1 (14-19), RST=54 | `ENABLE_WIFI=1` | `DEBUG_WIFI=1` |
| **Bootstrap Manager** | ✅ Active | - | SD_PWR=36 only | - | - |
| **NVS Flash** | ✅ Active | - | - | `ENABLE_NVS=1` | `DEBUG_NVS=0` |

### 🧪 Advanced Features and Tests

| Feature | Status | Flag | Requirements | Description |
|---------|--------|------|--------------|-------------|
| **WiFi Connection Test** | ✅ Available | `ENABLE_WIFI_CONNECT=1` | `wifi_config.h` | Connect to WiFi and display IP/RSSI |
| **RTC Read/Write Test** | ✅ Active | `ENABLE_RTC_TEST=1` | - | Display current RTC time |
| **RTC NTP Sync** | ✅ Fixed | `ENABLE_RTC_NTP_SYNC=0` | WiFi connected | Sync RTC with NTP server (callback-based) |
| **RTC Hardware Test** | ⚙️ Available | `ENABLE_RTC_HW_TEST=0` | - | Advanced RTC diagnostics |
| **Display RGB Test** | ⚙️ Available | `ENABLE_DISPLAY_TEST=0` | - | RGB test pattern |
| **Touch Input Test** | ⚙️ Available | `ENABLE_TOUCH_TEST=0` | - | Continuous touch reading |
| **I2C Bus Scan** | ❌ Disabled | `ENABLE_I2C_SCAN=0` | - | **DO NOT ENABLE** (interferes with GT911) |

### 📋 Expected Boot Log Output

| Initialization Stage | Tag | Expected Output | Time |
|---------------------|-----|-----------------|------|
| **Boot Info** | `app_init` | App version, compile time, ESP-IDF | ~1.4s |
| **BSP Phase A** | `BSP` | SD power control (GPIO36 only) | ~1.4-1.6s |
| **I2C Bus** | `GUITION_MAIN` | ✓ I2C bus ready (SDA=GPIO7, SCL=GPIO8) | ~1.6s |
| **ES8311 Audio** | `ES8311` | ✓ ES8311 initialized (Chip ID: 0x83) | ~1.7s |
| **RTC** | `RX8025T` | ✓ RTC initialized, Current time | ~1.7s |
| **Display** | `JD9165` | Display initialized (1024x600) | ~2.0s |
| **Touch** | `GT911` | ✓ GT911 initialized (1024x600) | ~2.1s |
| **Bootstrap Phase C** | `BOOTSTRAP` | WiFi Manager (SDMMC + C6 reset) | ~2.1-7.5s |
| **Bootstrap Phase B** | `BOOTSTRAP` | SD Manager (dummy init + mount) | ~7.5-7.8s |
| **SD Card Ready** | `GUITION_MAIN` | ✓ SD card mounted, Capacity | ~7.8s |
| **WiFi Connect** | `GUITION_MAIN` | ✓ WiFi connected! IP, RSSI | ~8.8s |

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
| GPIO36 | PWR_EN | SD card power control (BSP manages) |

### SDMMC Slot 1 (ESP-Hosted WiFi)

| ESP32-P4 Pin | Function | Owner |
|--------------|----------|-------|
| GPIO14-19 | SDIO Data | SDMMC driver |
| GPIO54 | C6_RESET | ESP-Hosted driver |
| GPIO6 | INT/READY | ESP-Hosted driver |

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
I (7770) GUITION_MAIN: === WiFi Connection Test ===
I (7771) GUITION_MAIN: Connecting to: FRITZ!Box 7530 WL
I (8829) GUITION_MAIN: ✓ WiFi connected!
I (8829) GUITION_MAIN:    IP: 192.168.188.88
I (8829) GUITION_MAIN:    Netmask: 255.255.255.0
I (8829) GUITION_MAIN:    Gateway: 192.168.188.1
I (8833) GUITION_MAIN:    RSSI: -67 dBm
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
- **Timeout**: 90 seconds (optimized for ESP-Hosted)
- **Success Detection**: Callback-based (fixes race condition)
- **Diagnostics**: DNS resolution, gateway ping, NTP server ping
- **Debug Mode**: Enhanced logging via `CONFIG_APP_NTP_DEBUG_ENABLE`

**Debug Configuration:**

For detailed NTP diagnostics, enable debug mode in `sdkconfig`:

```
CONFIG_APP_NTP_DEBUG_ENABLE=y
```

This enables:
- DNS resolution testing
- Gateway connectivity checks (ping)
- NTP server connectivity checks (ping)
- Detailed timing information
- Status transition logging

> [!IMPORTANT]
> **ESP-Hosted NTP Sync Fix (v1.3.0-dev)**
>
> NTP synchronization with ESP-Hosted requires callback-based success detection due to a race condition in SNTP status polling:
> - SNTP status briefly shows `COMPLETED` then resets to `RESET`
> - Callback invocation is the authoritative success indicator
> - Typical sync time: 12-13 seconds
> - See [troubleshooting.md](troubleshooting.md#rtc-ntp-synchronization-with-esp-hosted) for details

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
| **Hardware Button** | ⭐⭐⭐ | ✅ OK | ✅ OK | ⚙️ Soft reset | ⚠️ Works (C6 resets) |
| **USB Disconnect** | ⭐⭐ | ⚠️ May fail | ⚠️ May timeout | ⚙️ Hard reset cycle | ❌ Less reliable |

### Best Practices

1. **Use IDF Monitor restart** (`Ctrl+T` then `Ctrl+R`) for development
2. **Full power cycle** for production testing
3. **Hardware button** works reliably (C6 resets automatically on P4 boot)
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
- SDMMC host controller re-initialization attempted (Phase B must use "dummy init")
- Hardware button reset with inconsistent state
- USB disconnect/reconnect (power cycle for 5+ seconds)

**Solutions:**
1. Use `Ctrl+T` + `Ctrl+R` in IDF monitor (recommended)
2. Full power cycle (disconnect USB for 5+ seconds)
3. Check SD card is properly seated
4. Try different SD card (FAT32 formatted)

**See [troubleshooting.md](troubleshooting.md#system-reset-behavior-and-initialization-reliability) for complete analysis.**

#### Bootstrap timeout

**Symptoms:**
```
E (2617) BOOTSTRAP: Bootstrap timeout!
E (2635) BOOTSTRAP:   Phase C (WiFi Hosted) did not complete
```

**Note:** This is **expected behavior**. ESP-Hosted proceeds successfully despite timeout. Both WiFi and SD card work correctly.

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
4. Check firewall settings

#### NTP sync fails or reports failure despite callback

**Symptoms:**
```
I (xxxx) RTC_NTP: ✓ NTP callback invoked! Time synchronized
I (xxxx) RTC_NTP: Final status: RESET (callback invoked: YES)
```

**Note:** This is **expected behavior** with ESP-Hosted. The callback is the authoritative success indicator.

**Why it happens:**
- SNTP status briefly shows `COMPLETED` then immediately resets to `RESET`
- This is a race condition in ESP-IDF's SNTP implementation with ESP-Hosted
- The fix uses callback-based detection instead of status polling
- Sync is successful if callback was invoked, regardless of final status

**See [troubleshooting.md](troubleshooting.md#rtc-ntp-synchronization-with-esp-hosted) for complete details.**

### Important Notes

⚠️ **I2C Scan Must Be Disabled**
- `ENABLE_I2C_SCAN=0` is required
- Scanning interferes with GT911 touch controller
- Causes "clear bus failed" errors

⚠️ **Single lwIP Reference Required**
- CMakeLists.txt must have only ONE `lwip` in REQUIRES
- Duplicate causes WiFi instability and SD card errors

⚠️ **RTC NTP Sync Requires WiFi**
- Enable `ENABLE_WIFI_CONNECT=1` first
- Verify WiFi connection succeeds
- Then enable `ENABLE_RTC_NTP_SYNC=1`

⚠️ **BSP Manages Only GPIO36**
- GPIO54 (C6 reset) is owned by ESP-Hosted
- GPIO18 (SDIO CLK) is owned by SDMMC driver
- Never manipulate these pins outside their drivers

⚠️ **NTP Sync Status vs Callback**
- With ESP-Hosted, trust the callback, not the final status
- Status shows `RESET` after sync is a known race condition
- Callback invocation = successful sync

---

## 📚 Documentation

### Additional Resources

- **[troubleshooting.md](troubleshooting.md)** - Complete troubleshooting guide with:
  - BSP minimal architecture and driver ownership
  - Bootstrap Manager timing analysis
  - Reset behavior comparison
  - SD card `0x107` error root causes
  - I2C device initialization best practices
  - WiFi/ESP-Hosted debugging
  - **RTC NTP sync with ESP-Hosted (callback-based fix)**
  - Complete system boot logs
  - Hardware diagnostic procedures

- **[SDMMC_ARBITER_README.md](SDMMC_ARBITER_README.md)** - SDMMC bus arbitration details
- **[I2C_MIPI_DSI_CONFLICT.md](I2C_MIPI_DSI_CONFLICT.md)** - I2C and MIPI DSI conflict resolution
- **[RELEASE_NOTES.md](RELEASE_NOTES.md)** - Version history and changes
- **[CONTRIBUTING.md](CONTRIBUTING.md)** - Contribution guidelines

---

## 🗺️ Roadmap

### Current Status: Phase 1 Complete ✅

All onboard peripherals functional with deterministic bootstrap manager and comprehensive documentation.

### Phase 2: Testing & Software Suite Completion (Q2 2026)

#### 2.1 Onboard Hardware Testing
- [ ] Display, Touch, Audio, RTC, SD, WiFi comprehensive test suites

#### 2.2 External Peripherals Support
- [ ] I2C, SPI, UART, GPIO expansion modules

#### 2.3 Software Suite
- [ ] Automated test framework
- [ ] Diagnostic tools
- [ ] Example applications

### Phase 3: Architecture Refactoring (Q3 2026)

#### 3.1 Driver Isolation
- [ ] Separate hardware drivers from BSP logic
- [ ] Driver API standardization
- [ ] Hardware abstraction layer (HAL)

#### 3.2 BSP Component Architecture
- [ ] Reusable ESP-IDF component
- [ ] Public API design
- [ ] Bootstrap Manager as standalone component

#### 3.3 Business Logic Separation
- [ ] Demo applications restructuring
- [ ] Application-level features

### Phase 4: Advanced Integration (Q4 2026)

- [ ] LVGL integration
- [ ] Audio framework
- [ ] Networking & IoT
- [ ] Power management

### Phase 5: Production Readiness (2027)

- [ ] Documentation
- [ ] Quality assurance
- [ ] Community

**See [README.md Roadmap section](README.md#-roadmap) for complete details.**

---

## 🤝 Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

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
