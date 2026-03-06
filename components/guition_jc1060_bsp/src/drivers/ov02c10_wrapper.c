/*
 * OV02C10 Camera Wrapper (Phase 1: Detection)
 *
 * Copyright (c) 2026 Cristiano Gorla
 * SPDX-License-Identifier: Unlicense
 */

#include "drivers/ov02c10_wrapper.h"

#include <inttypes.h>
#include <stdbool.h>

#include "bsp_board.h"
#include "bsp_log_panel.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_ESP_ISP_ENABLE
#include "driver/isp_core.h"
#endif

#define LOG_UNIT "OV02C10"
#define LOGI(fmt, ...) BSP_LOGI_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) BSP_LOGW_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) BSP_LOGE_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)

static const char *TAG = BSP_LOG_TAG;

#ifndef CONFIG_BSP_PIN_CAMERA_XSHUTDN
#define CONFIG_BSP_PIN_CAMERA_XSHUTDN 20
#endif

#ifndef CONFIG_BSP_PIN_CAMERA_MCLK
#define CONFIG_BSP_PIN_CAMERA_MCLK 5
#endif

#ifndef CONFIG_BSP_CAMERA_I2C_ADDR
#define CONFIG_BSP_CAMERA_I2C_ADDR 0x6C
#endif

#ifndef CONFIG_BSP_CAMERA_EXPECTED_CHIP_ID
#define CONFIG_BSP_CAMERA_EXPECTED_CHIP_ID 0x560243
#endif

#ifndef CONFIG_BSP_CAMERA_FRAME_WIDTH
#define CONFIG_BSP_CAMERA_FRAME_WIDTH 1920
#endif

#ifndef CONFIG_BSP_CAMERA_FRAME_HEIGHT
#define CONFIG_BSP_CAMERA_FRAME_HEIGHT 1080
#endif

#ifndef CONFIG_BSP_I2C_FREQ_HZ
#define CONFIG_BSP_I2C_FREQ_HZ 400000
#endif

#define OV02C10_CHIP_ID_MSB_REG  0x300A
#define OV02C10_CHIP_ID_MID_REG  0x300B
#define OV02C10_CHIP_ID_LSB_REG  0x300C

static bool s_powered = false;
static bool s_initialized = false;
static i2c_master_dev_handle_t s_cam_dev_handle = NULL;

#if CONFIG_ESP_ISP_ENABLE
static isp_proc_handle_t s_isp_proc = NULL;
#endif

static inline uint8_t ov02c10_sccb_addr_7bit(void)
{
    /*
     * Board/docs define OV02C10 SCCB address as 0x6C (8-bit notation).
     * ESP-IDF I2C API requires 7-bit address, so convert by shifting right.
     */
    return (uint8_t)(((uint32_t)CONFIG_BSP_CAMERA_I2C_ADDR >> 1U) & 0x7FU);
}

static esp_err_t ov02c10_ensure_i2c_device(void)
{
    i2c_master_bus_handle_t i2c_bus = bsp_i2c_get_bus_handle();
    if (i2c_bus == NULL) {
        LOGE("I2C bus not ready, cannot probe OV02C10");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_cam_dev_handle != NULL) {
        return ESP_OK;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ov02c10_sccb_addr_7bit(),
        .scl_speed_hz = CONFIG_BSP_I2C_FREQ_HZ,
    };

    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_cam_dev_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to add OV02C10 to SCCB bus");

    LOGI("SCCB device ready (cfg addr=0x%02X, 7-bit=0x%02X)",
         (unsigned int)CONFIG_BSP_CAMERA_I2C_ADDR,
         (unsigned int)ov02c10_sccb_addr_7bit());
    return ESP_OK;
}

static esp_err_t ov02c10_read_reg8(uint16_t reg, uint8_t *value)
{
    uint8_t reg_addr[2] = {
        (uint8_t)((reg >> 8) & 0xFFU),
        (uint8_t)(reg & 0xFFU),
    };

    if (s_cam_dev_handle == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit_receive(s_cam_dev_handle, reg_addr, sizeof(reg_addr), value, 1, 1000);
}

static esp_err_t ov02c10_probe_chip_id(uint32_t *out_chip_id)
{
    esp_err_t ret;
    uint8_t id_msb = 0;
    uint8_t id_mid = 0;
    uint8_t id_lsb = 0;
    uint32_t chip_id;

    ret = ov02c10_read_reg8(OV02C10_CHIP_ID_MSB_REG, &id_msb);
    ESP_RETURN_ON_ERROR(ret, TAG, "SCCB read failed at reg 0x%04X", OV02C10_CHIP_ID_MSB_REG);

    ret = ov02c10_read_reg8(OV02C10_CHIP_ID_MID_REG, &id_mid);
    ESP_RETURN_ON_ERROR(ret, TAG, "SCCB read failed at reg 0x%04X", OV02C10_CHIP_ID_MID_REG);

    ret = ov02c10_read_reg8(OV02C10_CHIP_ID_LSB_REG, &id_lsb);
    ESP_RETURN_ON_ERROR(ret, TAG, "SCCB read failed at reg 0x%04X", OV02C10_CHIP_ID_LSB_REG);

    chip_id = ((uint32_t)id_msb << 16) | ((uint32_t)id_mid << 8) | (uint32_t)id_lsb;

    if (out_chip_id) {
        *out_chip_id = chip_id;
    }

    if (chip_id != (uint32_t)CONFIG_BSP_CAMERA_EXPECTED_CHIP_ID) {
        uint32_t expected = (uint32_t)CONFIG_BSP_CAMERA_EXPECTED_CHIP_ID;
        bool same_family = (chip_id & 0xFFFF00U) == (expected & 0xFFFF00U);

        if (!same_family) {
            LOGE("[OV02C10] Sensor ID mismatch. Expected: 0x%06" PRIX32 ", got: 0x%06" PRIX32,
                 expected,
                 chip_id);
            return ESP_ERR_NOT_FOUND;
        }

        LOGW("[OV02C10] Sensor revision differs from expected. Expected: 0x%06" PRIX32 ", got: 0x%06" PRIX32,
             expected,
             chip_id);
    }

    LOGI("[OV02C10] ✓ Sensor detected. ID: 0x%06" PRIX32 ".", chip_id);
    return ESP_OK;
}

#if CONFIG_ESP_ISP_ENABLE
static esp_err_t ov02c10_register_isp_instance(void)
{
    if (s_isp_proc != NULL) {
        return ESP_OK;
    }

    esp_isp_processor_cfg_t isp_cfg = {
        .clk_src = ISP_CLK_SRC_DEFAULT,
        .clk_hz = 160000000,
        .input_data_source = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type = ISP_COLOR_RAW10,
        .output_data_color_type = ISP_COLOR_RAW10,
        .yuv_range = ISP_COLOR_RANGE_FULL,
        .yuv_std = ISP_YUV_CONV_STD_BT601,
        .has_line_start_packet = false,
        .has_line_end_packet = false,
        .h_res = CONFIG_BSP_CAMERA_FRAME_WIDTH,
        .v_res = CONFIG_BSP_CAMERA_FRAME_HEIGHT,
        .bayer_order = COLOR_RAW_ELEMENT_ORDER_BGGR,
        .intr_priority = 0,
        .flags = {
            .bypass_isp = 1,
            .byte_swap_en = 0,
        },
    };

    esp_err_t ret = esp_isp_new_processor(&isp_cfg, &s_isp_proc);
    if (ret != ESP_OK) {
        LOGE("Failed to register ISP instance: %s", esp_err_to_name(ret));
        return ret;
    }

    LOGI("[OV02C10] ISP instance registered (CSI input, %ux%u)",
         (unsigned int)CONFIG_BSP_CAMERA_FRAME_WIDTH,
         (unsigned int)CONFIG_BSP_CAMERA_FRAME_HEIGHT);
    return ESP_OK;
}
#endif

esp_err_t bsp_camera_power_on(void)
{
    gpio_config_t xshutdown_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BSP_PIN_CAMERA_XSHUTDN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t mclk_conf = {
        .pin_bit_mask = (1ULL << CONFIG_BSP_PIN_CAMERA_MCLK),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (s_powered) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(gpio_config(&xshutdown_conf), TAG, "Failed to configure XSHUTDN GPIO");
    ESP_RETURN_ON_ERROR(gpio_config(&mclk_conf), TAG, "Failed to configure MCLK GPIO");

    /* Keep MCLK pin in known output state for Phase 1 bring-up. */
    gpio_set_level(CONFIG_BSP_PIN_CAMERA_MCLK, 1);

    /*
     * OV02C10 reset/power-down sequence:
     * LOW -> wait 5ms -> HIGH.
     *
     * Note: GPIO20 (XSHUTDN) must remain dedicated to camera reset and must
     * not be confused with slave SDIO_DATA0 naming from board schematics.
     */
    gpio_set_level(CONFIG_BSP_PIN_CAMERA_XSHUTDN, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(CONFIG_BSP_PIN_CAMERA_XSHUTDN, 1);

    s_powered = true;
    LOGI("[Phase A] Camera power/reset sequence done (XSHUTDN=%d, MCLK=%d)",
         CONFIG_BSP_PIN_CAMERA_XSHUTDN,
         CONFIG_BSP_PIN_CAMERA_MCLK);
    return ESP_OK;
}

esp_err_t bsp_camera_init(void)
{
    uint32_t chip_id = 0;
    esp_err_t ret;

    if (s_initialized) {
        return ESP_OK;
    }

    if (!s_powered) {
        ESP_RETURN_ON_ERROR(bsp_camera_power_on(), TAG, "Camera power-on failed");
    }

    ret = ov02c10_ensure_i2c_device();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = ov02c10_probe_chip_id(&chip_id);
    if (ret != ESP_OK) {
        if (s_cam_dev_handle) {
            i2c_master_bus_rm_device(s_cam_dev_handle);
            s_cam_dev_handle = NULL;
        }
        return ESP_ERR_INVALID_STATE;
    }

#if CONFIG_ESP_ISP_ENABLE
    ESP_RETURN_ON_ERROR(ov02c10_register_isp_instance(), TAG, "ISP registration failed");
#else
    LOGW("ISP disabled in sdkconfig; skipping ISP instance registration");
#endif

    s_initialized = true;
    return ESP_OK;
}

void bsp_camera_deinit(void)
{
#if CONFIG_ESP_ISP_ENABLE
    if (s_isp_proc) {
        esp_isp_del_processor(s_isp_proc);
        s_isp_proc = NULL;
    }
#endif

    if (s_cam_dev_handle) {
        i2c_master_bus_rm_device(s_cam_dev_handle);
        s_cam_dev_handle = NULL;
    }

    s_initialized = false;
    s_powered = false;
}
