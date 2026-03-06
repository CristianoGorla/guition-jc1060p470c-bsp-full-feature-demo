/*
 * OV02C10 Camera Wrapper (Phase 2: Link + Streaming)
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
#include "driver/ledc.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_csi.h"
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
#define OV02C10_REG_MODE_SELECT  0x0100

#define OV02C10_MCLK_HZ                  24000000U
#define OV02C10_MCLK_STABLE_CYCLES       8192U
#define OV02C10_MIPI_DATA_LANES          2U
#define OV02C10_MIPI_LANE_BITRATE_MBPS   800
#define OV02C10_MIPI_RAW10_DT            0x2B

static bool s_powered = false;
static bool s_initialized = false;
static bool s_streaming = false;
static bool s_mclk_running = false;
static i2c_master_dev_handle_t s_cam_dev_handle = NULL;
static esp_cam_ctlr_handle_t s_csi_ctlr = NULL;

#if CONFIG_ESP_ISP_ENABLE
static isp_proc_handle_t s_isp_proc = NULL;
#endif

typedef struct {
    uint16_t reg;
    uint8_t value;
    uint16_t delay_ms;
} ov02c10_reg_setting_t;

/*
 * 1080p@60 bring-up profile (to be extended with full tuning tables as needed).
 * The final MODE_SELECT write (0x0100=0x01) is issued explicitly at stream start.
 */
static const ov02c10_reg_setting_t s_ov02c10_1080p60_regs[] = {
    {0x0103, 0x01, 5}, /* software reset */
    {0x0100, 0x00, 1}, /* standby */
    {0x3012, 0x01, 0},
    {0x3013, 0x12, 0},
    {0x3808, 0x07, 0}, /* width high: 1920 */
    {0x3809, 0x80, 0}, /* width low */
    {0x380A, 0x04, 0}, /* height high: 1080 */
    {0x380B, 0x38, 0}, /* height low */
};

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

static esp_err_t ov02c10_write_reg8(uint16_t reg, uint8_t value)
{
    uint8_t payload[3] = {
        (uint8_t)((reg >> 8) & 0xFFU),
        (uint8_t)(reg & 0xFFU),
        value,
    };

    if (s_cam_dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_transmit(s_cam_dev_handle, payload, sizeof(payload), 1000);
}

static esp_err_t ov02c10_enable_mclk_24mhz(void)
{
    if (s_mclk_running) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_1_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = OV02C10_MCLK_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to configure OV02C10 MCLK timer");

    ledc_channel_config_t chan_cfg = {
        .gpio_num = CONFIG_BSP_PIN_CAMERA_MCLK,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 1,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&chan_cfg);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to route OV02C10 MCLK to GPIO");

    /* 8192 cycles @24MHz ~= 341us; wait 1ms for safe margin before SCCB ops. */
    (void)OV02C10_MCLK_STABLE_CYCLES;
    vTaskDelay(pdMS_TO_TICKS(1));

    s_mclk_running = true;
    LOGI("MCLK running on GPIO %d @ %u Hz", CONFIG_BSP_PIN_CAMERA_MCLK, OV02C10_MCLK_HZ);
    return ESP_OK;
}

static esp_err_t ov02c10_init_csi_link(void)
{
    if (s_csi_ctlr != NULL) {
        return ESP_OK;
    }

    esp_cam_ctlr_csi_config_t csi_cfg = {
        .ctlr_id = 0,
        .clk_src = MIPI_CSI_PHY_CLK_SRC_DEFAULT,
        .h_res = CONFIG_BSP_CAMERA_FRAME_WIDTH,
        .v_res = CONFIG_BSP_CAMERA_FRAME_HEIGHT,
        .data_lane_num = OV02C10_MIPI_DATA_LANES,
        .lane_bit_rate_mbps = OV02C10_MIPI_LANE_BITRATE_MBPS,
        .input_data_color_type = CAM_CTLR_COLOR_RAW10,
        .output_data_color_type = CAM_CTLR_COLOR_RAW10,
        .queue_items = 1,
        .byte_swap_en = 0,
        .bk_buffer_dis = 0,
    };

    esp_err_t ret = esp_cam_new_csi_ctlr(&csi_cfg, &s_csi_ctlr);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create CSI controller");

    ret = esp_cam_ctlr_enable(s_csi_ctlr);
    if (ret != ESP_OK) {
        esp_cam_ctlr_del(s_csi_ctlr);
        s_csi_ctlr = NULL;
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to enable CSI controller");
    }

    LOGI("CSI controller configured: lanes=%u, bitrate=%dMbps/lane, dt=0x%02X (RAW10)",
         (unsigned int)OV02C10_MIPI_DATA_LANES,
         OV02C10_MIPI_LANE_BITRATE_MBPS,
         OV02C10_MIPI_RAW10_DT);
    return ESP_OK;
}

static esp_err_t ov02c10_apply_stream_profile(void)
{
    for (size_t i = 0; i < (sizeof(s_ov02c10_1080p60_regs) / sizeof(s_ov02c10_1080p60_regs[0])); i++) {
        const ov02c10_reg_setting_t *reg = &s_ov02c10_1080p60_regs[i];
        esp_err_t ret = ov02c10_write_reg8(reg->reg, reg->value);
        if (ret != ESP_OK) {
            LOGE("SCCB write failed at reg 0x%04X", reg->reg);
            return ret;
        }

        if (reg->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(reg->delay_ms));
        }
    }

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

    if (s_powered) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(gpio_config(&xshutdown_conf), TAG, "Failed to configure XSHUTDN GPIO");
    ESP_RETURN_ON_ERROR(ov02c10_enable_mclk_24mhz(), TAG, "Failed to start camera MCLK");

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
        LOGI("[Phase A] Camera power/reset sequence done (XSHUTDN=%d, MCLK=%d @ %uHz)",
         CONFIG_BSP_PIN_CAMERA_XSHUTDN,
            CONFIG_BSP_PIN_CAMERA_MCLK,
            OV02C10_MCLK_HZ);
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

    ret = ov02c10_init_csi_link();
    if (ret != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = bsp_camera_start_stream();
    if (ret != ESP_OK) {
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

esp_err_t bsp_camera_start_stream(void)
{
    if (s_streaming) {
        return ESP_OK;
    }

    if (s_cam_dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(ov02c10_apply_stream_profile(), TAG, "Failed to apply 1080p60 register profile");

    LOGI("[OV02C10] MIPI Link ready: 2-lane @ 800Mbps. Starting stream...");
    ESP_RETURN_ON_ERROR(ov02c10_write_reg8(OV02C10_REG_MODE_SELECT, 0x01), TAG,
                        "Failed to set OV02C10 stream mode");

    if (s_csi_ctlr) {
        ESP_RETURN_ON_ERROR(esp_cam_ctlr_start(s_csi_ctlr), TAG, "Failed to start CSI controller");
    }

    s_streaming = true;
    return ESP_OK;
}

void bsp_camera_deinit(void)
{
    if (s_csi_ctlr) {
        esp_cam_ctlr_stop(s_csi_ctlr);
        esp_cam_ctlr_disable(s_csi_ctlr);
        esp_cam_ctlr_del(s_csi_ctlr);
        s_csi_ctlr = NULL;
    }

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

    if (s_mclk_running) {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    }

    s_streaming = false;
    s_initialized = false;
    s_powered = false;
    s_mclk_running = false;
}
