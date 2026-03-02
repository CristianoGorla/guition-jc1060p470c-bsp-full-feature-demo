# Guition JC1060P470C BSP - Full Feature Demo

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.3-blue)](https://github.com/espressif/esp-idf)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-ESP32--P4-orange)](https://www.espressif.com/en/products/socs/esp32-p4)
[![Latest Release](https://img.shields.io/badge/release-v1.3.0-brightgreen)](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/releases/tag/v1.3.0)
[![Development](https://img.shields.io/badge/dev-v1.3.0-yellow)](https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo/tree/develop/v1.3.0)

**Complete Board Support Package demonstration for the Guition JC1060P470C_I_W_Y development board featuring ESP32-P4.**

**v1.3.0**: Complete BSP refactor - all drivers, bootstrap and utils moved to dedicated component with Kconfig configuration.

---

## 📋 Versions

### Latest Release **v1.3.0** ✅

**Branch: `develop/v1.3.0`**

**Key Changes:**
- ✅ **BSP Component**: `components/guition_jc1060_bsp/` with `drivers/`, `bootstrap/`, `utils/`
- ✅ **Kconfig Configuration**: Replaced `feature_flags.h` with `idf.py menuconfig`
- ✅ **Driver Naming**: `rx8025t_bsp.h`, `es8311_bsp.h`, `gt911_bsp.h`, etc.
- ✅ **main/**: Pure application code only (main.c)
- ✅ All compilation issues fixed

**Installation:**
```bash
git clone --branch develop/v1.3.0 https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo.git
cd guition-jc1060p470c-bsp-full-feature-demo
idf.py set-target esp32p4
idf.py build flash monitor
```

**See [RELEASE_NOTES.md](RELEASE_NOTES.md) for complete changelog.**

---

## ⚙️ Configuration

**Use `idf.py menuconfig` for all configuration:**

```bash
idf.py menuconfig
```

### BSP Features (`Component config → Guition JC1060 BSP`)
```
BSP_ENABLE_RTC           [*] RTC (RX8025T)
BSP_ENABLE_AUDIO         [*] Audio (ES8311)  
BSP_ENABLE_TOUCH         [*] Touch (GT911)
BSP_ENABLE_DISPLAY       [*] Display (JD9165)
BSP_ENABLE_SD_CARD       [*] SD Card
BSP_ENABLE_WIFI_HOSTED   [*] WiFi (ESP-Hosted)
```

### Application Features (`Component config → Example Configuration`)
```
APP_ENABLE_WIFI_CONNECT    [ ] WiFi connection test  
APP_ENABLE_RTC_NTP_SYNC    [ ] NTP time sync
APP_ENABLE_RTC_TEST        [ ] RTC validation
APP_ENABLE_SD_TEST         [ ] SD card tests
```

**WiFi credentials:** Copy `main/wifi_config.h.example` → `main/wifi_config.h`

---

## 🎯 Quick Start

```bash
git clone --branch develop/v1.3.0 https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo.git
cd guition-jc1060p470c-bsp-full-feature-demo
idf.py set-target esp32p4
idf.py build flash monitor
```

**Expected boot sequence (7.8s total):**
```
✓ I2C bus ready (GPIO7/8)
✓ ES8311 initialized (0x18)
✓ RX8025T RTC ready (0x32) 
✓ JD9165 display (1024x600)
✓ GT911 touch ready (0x14)
✓ Bootstrap complete (Phase A→C→B)
✓ SD card mounted (/sdcard)
✓ ESP-Hosted WiFi ready
```

## 📁 Project Structure (v1.3.0)

```
components/
└── guition_jc1060_bsp/          ← **NEW: Complete BSP component**
    ├── drivers/                 ← All hardware drivers
    │   ├── rx8025t_bsp.[ch]    ← RTC driver  
    │   ├── es8311_bsp.[ch]     ← Audio driver
    │   ├── gt911_bsp.[ch]      ← Touch driver
    │   └── jd9165_bsp.[ch]     ← Display driver
    ├── bootstrap/              ← SDMMC arbitration
    │   ├── bootstrap_manager.[ch]
    │   └── sdmmc_arbiter.[ch]
    ├── utils/                  ← Test utilities
    │   ├── rtc_test.c          ← ✅ Fixed compilation
    │   └── sd_card_functions.c
    ├── Kconfig.projbuild       ← BSP configuration
    └── CMakeLists.txt

main/                           ← **Pure application only**
    ├── main.c                  ← Entry point
    ├── wifi_config.h           ← WiFi credentials (.gitignore)
    └── Kconfig.projbuild       ← App features
```

**Migration from v1.0.0-beta:**
- `main/bootstrap/` → `components/guition_jc1060_bsp/bootstrap/`
- `main/utils/` → `components/guition_jc1060_bsp/utils/`
- `feature_flags.h` → Kconfig (menuconfig)
- All drivers → `*_bsp.[ch]` naming

---

## 🛠️ BSP Architecture

### Three-Phase Bootstrap (Unchanged)

```
Phase A (BSP): SD power control (GPIO36 only)
Phase C: ESP-Hosted WiFi (SDMMC Slot 1, GPIO54 auto-reset)
Phase B: SD Card (SDMMC Slot 0, dummy init)
```

**BSP manages ONLY GPIO36** - all other GPIOs owned by drivers [TROUBLESHOOTING.md].

### Hardware Status

| Peripheral | Driver File | I2C Addr | Status |
|------------|-------------|----------|--------|
| **RTC** | `rx8025t_bsp.h` | 0x32 | ✅ |
| **Audio** | `es8311_bsp.h` | 0x18 | ✅ |
| **Touch** | `gt911_bsp.h` | 0x14 | ✅ |
| **Display** | `jd9165_bsp.c` | DSI | ✅ |

---

## 📚 Documentation

- **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)** ← **Most important**
- **[SDMMC_ARBITER_README.md](SDMMC_ARBITER_README.md)**
- **[RELEASE_NOTES.md](RELEASE_NOTES.md)** ← v1.3.0 changelog
- **[CONTRIBUTING.md](CONTRIBUTING.md)**

## 🐛 Troubleshooting

**Most common issues:**
1. **SD `0x107`**: Use IDF monitor restart (`Ctrl+T Ctrl+R`)
2. **GT911 "clear bus"**: `ENABLE_I2C_SCAN=0` in menuconfig
3. **Compilation**: Ensure `develop/v1.3.0` branch

**Full guide:** [TROUBLESHOOTING.md](TROUBLESHOOTING.md)

---

**Status**: ✅ **Production Ready** | **Updated**: 2026-03-02 v1.3.0