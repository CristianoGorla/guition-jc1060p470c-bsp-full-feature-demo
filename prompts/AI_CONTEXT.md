# AI Context Loading Prompt for Troubleshooting

Use this prompt to load complete project context in a new AI chat session for troubleshooting.

---

# Guition JC1060P470C BSP - Full Feature Demo Troubleshooting Context

I'm working on the **Guition JC1060P470C** (ESP32-P4 + ESP32-C6 WiFi) development board with a complete BSP implementation.

## 📦 **Repository Information**

### Main Project (My Implementation - PRIMARY REFERENCE)
**Repository:** `CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo`
**Branch:** `feature/lvgl-v9-integration`
**GitHub:** https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo
**Status:** ✅ Fully tested and documented

**⚠️ CRITICAL:** For GPIO pins, ALWAYS refer to my project documentation. Pin assignments are verified and tested.

### Vendor Demo (Reference for Driver Initialization Logic)
**Repository:** `CristianoGorla/GUITION-JC1060P470C_I_W_Y`
**Path:** `JC1060P470C_I_W_Y/1-Demo/Demo_IDF/ESP-IDF/lvgl_demo_v9`
**GitHub:** https://github.com/CristianoGorla/GUITION-JC1060P470C_I_W_Y
**Vendor:** Guition (official manufacturer)
**Status:** ✅ Working implementation, good for driver initialization patterns

**Purpose:** Use this for understanding driver initialization logic and BSP patterns (clones existing ESP-IDF board). DO NOT use for pin assignments.

### Additional Reference (Community Project)
**Repository:** `cheops/JC1060P470C_I_W`
**GitHub:** https://github.com/cheops/JC1060P470C_I_W
**Purpose:** Alternative implementation for comparison. DO NOT use for pin assignments.

## 📚 **Critical Documentation Files to Review**

Please access and review these files from my repository (`CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo`, branch `feature/lvgl-v9-integration`):

### 🎯 Core Architecture & Hardware (START HERE)
1. **README.md** - Complete project overview, hardware specs, **VERIFIED PIN MAPPING**
2. **SDMMC_ARBITER_README.md** - SDMMC controller management (Slot 0: SD Card, Slot 1: WiFi)
3. **TROUBLESHOOTING.md** - Comprehensive troubleshooting guide with complete solutions

### 🔄 Bootstrap & Initialization
4. **docs/BOOTSTRAP_SEQUENCE.md** - 3-phase initialization sequence (Phase A → C → B)
5. **docs/BOOTSTRAP_STRATEGY.md** - Bootstrap design rationale and architecture
6. **docs/ESP_HOSTED_RESET_STRATEGIES.md** - C6 reset timing and ESP-Hosted initialization

### 🎨 LVGL Integration
7. **README_LVGL.md** - LVGL v9.2.2 integration guide
8. **docs/LVGL_V9_FRAME_BUFFER_FIX.md** - Frame buffer alignment fix for LVGL v9

### 🐛 Known Issues & Fixes
9. **I2C_MIPI_DSI_CONFLICT.md** - I2C/MIPI DSI GPIO conflict resolution
10. **docs/WIFI_FIRST_BOOT_ISSUE.md** - WiFi first boot timing issue (C6 firmware delay)
11. **docs/PROJECT_STATUS.md** - Current project status and roadmap

### 👥 Contributing & Coding Standards
12. **CONTRIBUTING.md** - Development guidelines
13. **SANITIZATION_RULES.md** - Code sanitization rules
14. **RELEASE_NOTES.md** - Version history and changes

### ⚙️ Configuration
15. **sdkconfig.defaults** - ESP-IDF configuration baseline
16. **main/feature_flags.h** - Peripheral enable/disable flags

## 🔧 **Key Source Files**

### Bootstrap System
- `components/guition_jc1060_bsp/bootstrap/bootstrap_manager.c` - 3-phase orchestrator
- `components/guition_jc1060_bsp/bootstrap/sd_card_manager.c` - SD mount with controller reinit
- `components/guition_jc1060_bsp/bootstrap/esp_hosted_wifi.c` - WiFi transport initialization
- `components/guition_jc1060_bsp/bootstrap/sdmmc_arbiter.c` - Runtime slot switching API

### BSP Layer
- `components/guition_jc1060_bsp/bsp_board.c` - BSP minimal architecture (GPIO36 only)

### Peripheral Drivers
- `components/guition_jc1060_bsp/peripherals/` - Display, Touch, Audio, RTC drivers

## 🔌 **Hardware Specifications (VERIFIED)**

### Board
- **Model:** Guition JC1060P470C_I_W_Y
- **Main MCU:** ESP32-P4 @ 360MHz (Dual-core RISC-V)
- **WiFi MCU:** ESP32-C6 (via ESP-Hosted SDIO)
- **PSRAM:** 32MB @ 200MHz (Octal SPI)
- **Flash:** 16MB @ 40MHz (Quad SPI)

### Peripherals
- **Display:** JD9165 4.7" 1024x600 MIPI DSI
- **Touch:** GT911 I2C capacitive (up to 5 points)
- **Audio:** ES8311 I2S codec + speaker amplifier
- **RTC:** RX8025T I2C with battery backup
- **Storage:** MicroSD card (SDMMC 4-bit)

### Pin Assignments (VERIFIED - Use These!)

#### SDMMC Slot 0 (SD Card)
```
GPIO43 - CLK       (10k pullup)
GPIO44 - CMD       (10k pullup)
GPIO39 - D0        (10k pullup)
GPIO40 - D1        (10k pullup)
GPIO41 - D2        (10k pullup)
GPIO42 - D3        (10k pullup required)
GPIO36 - PWR_EN    (BSP manages SD power)
```

#### SDMMC Slot 1 (ESP-Hosted WiFi C6)
```
GPIO14-19 - SDIO Data/CMD/CLK
GPIO54    - C6_RESET (ESP-Hosted driver manages)
GPIO6     - INT/READY (ESP-Hosted interrupt)
```

#### I2C Bus (400 kHz)
```
GPIO7  - SDA
GPIO8  - SCL

I2C Devices:
- 0x14: GT911 Touch Controller
- 0x18: ES8311 Audio Codec
- 0x32: RX8025T RTC
```

#### Other
```
GPIO21 - Touch Reset (GT911 hardware reset)
GPIO22 - Touch Interrupt (GT911 touch events)
GPIO11 - Audio PA Control (speaker amplifier enable)
GPIO45-52 - MIPI DSI (display interface)
```

## 🏗️ **Key Architecture Points**

### 3-Phase Bootstrap Sequence
```
Phase A (Power Manager - BSP)
  ├─ SD card power control (GPIO36 only)
  ├─ Wait 100ms for rail stabilization
  └─ Signal: POWER_READY

Phase C (WiFi Manager - Priority 23)
  ├─ Wait for POWER_READY
  ├─ Initialize ESP-Hosted SDIO transport (Slot 1)
  │  └─ sdmmc_host_init() for Slot 1
  ├─ Wait 2000ms for SDIO link stabilization
  └─ Signal: WIFI_READY

Phase B (SD Manager - Priority 22)
  ├─ Wait for WIFI_READY
  ├─ CRITICAL: Reinitialize SDMMC controller for Slot 0
  │  ├─ sdmmc_host_deinit() (release Slot 1)
  │  ├─ Wait 200ms for bus settling
  │  ├─ sdmmc_host_init() (reinit for Slot 0)
  │  └─ sdmmc_host_init_slot(SLOT_0, ...)
  ├─ Mount SD card filesystem
  └─ Signal: SD_READY
```

### SDMMC Controller Management
- **Single Hardware Controller** shared between Slot 0 (SD) and Slot 1 (WiFi)
- **Must reinitialize** when switching between slots
- **NO bus sharing** - controller operates on one slot at a time

### BSP Minimal Architecture
**BSP Only Manages:**
- ✅ GPIO36 (SD card power enable)

**BSP Does NOT Manage:**
- ❌ GPIO54 (C6 reset) - Owned by ESP-Hosted driver
- ❌ GPIO14-19 (SDIO signals) - Owned by SDMMC driver
- ❌ GPIO18 (SDIO CLK) - Strapping pin, cannot force

## 🔨 **Recent Fixes (v1.3.0-dev)**

### Commit b9b77b6 (2026-03-03)
**Fix: SDMMC Controller Reinitialization for SD Slot 0**
- **Problem:** SD mount failing with timeout 0x107
- **Root Cause:** Controller initialized for Slot 1 (WiFi) but not reinitialized for Slot 0 (SD)
- **Solution:** Added proper deinit/reinit sequence in `sd_card_manager.c`
- **Result:** SD card now mounts successfully after WiFi initialization

### Commit bbbac49 (2026-03-03)
**docs: Fix SDMMC slot assignments in arbiter documentation**
- Corrected slot assignments in SDMMC_ARBITER_README.md
- Updated to reflect Slot 0 (SD) and Slot 1 (WiFi) architecture

### Commit [current] (2026-03-03)
**docs: Add AI context loading prompt**
- Created AI_CONTEXT_PROMPT.md for easy troubleshooting context loading

## 📊 **Expected Boot Sequence (Success)**

```
I (1411) BSP: [PHASE A] Configuring SD card power control (GPIO36)
I (2089) BOOTSTRAP: [Phase C] Starting WiFi transport...
I (2089) BOOTSTRAP: [Phase C] init_wifi() will initialize SDMMC controller
I (5836) sdio_wrapper: SDIO master: Slot 1  ← WiFi OK
I (7466) BOOTSTRAP: [Phase C] ✓ WIFI_READY (SDMMC controller initialized)
I (7467) BOOTSTRAP: [Phase B] Starting SD card mount...
I (7467) SD_MANAGER: Deinitializing SDMMC controller (release Slot 1)
I (7667) SD_MANAGER: Reinitializing SDMMC controller for Slot 0
I (7767) SD_MANAGER: Initializing SDMMC Slot 0
I (7827) SD_MANAGER: ✓ SD card mounted successfully
I (7827) SD_MANAGER:    Card: SA32G
I (7827) SD_MANAGER:    Capacity: 7580 MB
I (7833) BOOTSTRAP: ✓ Bootstrap COMPLETE (6.36s)
```

## 🎯 **Vendor Demo Reference Points**

### What to Learn from Vendor Demo
From `CristianoGorla/GUITION-JC1060P470C_I_W_Y/JC1060P470C_I_W_Y/1-Demo/Demo_IDF/ESP-IDF/lvgl_demo_v9`:

1. **Driver Initialization Patterns** - How drivers are initialized and configured
2. **BSP Cloning Approach** - How vendor clones existing ESP-IDF board support
3. **LVGL Integration** - Display buffer management and LVGL setup
4. **Timing and Delays** - Peripheral initialization timing

### What NOT to Use from Vendor Demo
- ❌ Pin assignments (may differ or be incomplete)
- ❌ SDMMC slot configuration (use my implementation)
- ❌ Bootstrap sequence (use my 3-phase approach)

**ALWAYS cross-reference with my project's documentation for final implementation.**

## ❓ **What I Need Help With**

[Describe your current issue here, including error logs if available]

### Attach Logs
```
[Paste your serial monitor output here]
```

### Environment
- ESP-IDF Version: v5.5.3-dirty
- LVGL Version: v9.2.2
- Target: ESP32-P4
- Branch: feature/lvgl-v9-integration
- Build: [paste git commit hash]

## 📋 **Debugging Checklist**

Before requesting help, verify:
- [ ] Using correct branch: `feature/lvgl-v9-integration`
- [ ] Read relevant documentation files (README.md, TROUBLESHOOTING.md)
- [ ] Checked pin assignments match my project (not vendor demo)
- [ ] Reviewed recent fixes (commits b9b77b6, bbbac49)
- [ ] Included complete serial output logs
- [ ] Noted which peripherals are enabled in `main/feature_flags.h`

## 🚀 **Quick Commands**

```bash
# Clone and build
git clone https://github.com/CristianoGorla/guition-jc1060p470c-bsp-full-feature-demo.git
cd guition-jc1060p470c-bsp-full-feature-demo
git checkout feature/lvgl-v9-integration
idf.py set-target esp32p4
idf.py build flash monitor

# Update to latest
git pull origin feature/lvgl-v9-integration
idf.py build flash monitor

# Enable debug logs
idf.py menuconfig
# Component config → Log output → Default log verbosity → Debug

# View specific component logs
esp_log_level_set("BOOTSTRAP", ESP_LOG_DEBUG);
esp_log_level_set("SD_MANAGER", ESP_LOG_DEBUG);
esp_log_level_set("wifi_hosted", ESP_LOG_DEBUG);
```

---

## 📝 **How to Use This Prompt**

1. Copy this entire file content (or the prompt section)
2. Paste at the start of a new AI chat session (Perplexity, ChatGPT, Claude, etc.)
3. Fill in the "What I Need Help With" section with your specific issue
4. Attach logs as files or paste in code blocks
5. Submit

The AI will automatically load context from the GitHub repositories via MCP tools and provide troubleshooting based on the complete project architecture.

---

**Note:** This prompt is designed for AI assistants with GitHub MCP (Model Context Protocol) access. For manual troubleshooting, refer to [TROUBLESHOOTING.md](./TROUBLESHOOTING.md).
