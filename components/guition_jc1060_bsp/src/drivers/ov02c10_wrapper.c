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
#include "soc/soc_caps.h"

#if CONFIG_ESP_ISP_ENABLE
#include "driver/isp.h"
#include "esp_heap_caps.h"
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
#define OV02C10_REG_EXPO_H        0x3501
#define OV02C10_REG_EXPO_L        0x3502

#define OV02C10_MCLK_HZ                 24000000U
#define OV02C10_MCLK_STABLE_CYCLES      8192U
#define OV02C10_MIPI_DATA_LANES         2U
#define OV02C10_MIPI_LANE_BITRATE_MBPS  800
#define OV02C10_MIPI_RAW10_DT           0x2B
#define OV02C10_MCLK_LEDC_CLK_CFG       LEDC_USE_PLL_DIV_CLK

#define OV02C10_ISP_AE_TARGET_LUMA      96U
#define OV02C10_ISP_AE_HYSTERESIS       10U
#define OV02C10_ISP_AE_EXPO_MIN         0x0100U
#define OV02C10_ISP_AE_EXPO_MAX         0x0FFFU
#define OV02C10_ISP_AE_EXPO_DEFAULT     0x0400U
#define OV02C10_ISP_AWB_GAIN_BASE       256U
#define OV02C10_ISP_AWB_GAIN_MIN        64U
#define OV02C10_ISP_AWB_GAIN_MAX        1023U
#define OV02C10_ISP_TASK_STACK          4096
#define OV02C10_ISP_TASK_PRIO           5

#if SOC_LEDC_SUPPORT_HS_MODE
#define OV02C10_MCLK_LEDC_SPEED_MODE    LEDC_HIGH_SPEED_MODE
#define OV02C10_MCLK_LEDC_TIMER         LEDC_TIMER_0
#define OV02C10_MCLK_LEDC_CHANNEL       LEDC_CHANNEL_0
#else
#define OV02C10_MCLK_LEDC_SPEED_MODE    LEDC_LOW_SPEED_MODE
#define OV02C10_MCLK_LEDC_TIMER         LEDC_TIMER_3
#define OV02C10_MCLK_LEDC_CHANNEL       LEDC_CHANNEL_3
#endif

static bool s_powered = false;
static bool s_initialized = false;
static bool s_streaming = false;
static bool s_mclk_running = false;
static i2c_master_dev_handle_t s_cam_dev_handle = NULL;
static esp_cam_ctlr_handle_t s_csi_ctlr = NULL;

#if CONFIG_ESP_ISP_ENABLE
static isp_proc_handle_t s_isp_proc = NULL;
static isp_ae_ctlr_t s_isp_ae_ctlr = NULL;
static isp_awb_ctlr_t s_isp_awb_ctlr = NULL;
static TaskHandle_t s_isp_ctrl_task = NULL;
static bool s_isp_task_running = false;
static bool s_isp_demosaic_enabled = false;
static bool s_isp_ccm_enabled = false;
static bool s_isp_blc_enabled = false;
static bool s_isp_wbg_enabled = false;
static uint16_t s_ae_exposure_reg = OV02C10_ISP_AE_EXPO_DEFAULT;
static bool s_ae_update_pending = false;
static bool s_awb_update_pending = false;
static isp_wbg_gain_t s_awb_gain_pending = {
    .gain_r = OV02C10_ISP_AWB_GAIN_BASE,
    .gain_g = OV02C10_ISP_AWB_GAIN_BASE,
    .gain_b = OV02C10_ISP_AWB_GAIN_BASE,
};
static void *s_isp_rgb888_frame = NULL;
static size_t s_isp_rgb888_frame_size = 0;
static portMUX_TYPE s_isp_lock = portMUX_INITIALIZER_UNLOCKED;
#endif

typedef struct {
    uint16_t reg;
    uint8_t value;
    uint16_t delay_ms;
} ov02c10_reg_setting_t;

static IRAM_ATTR bool ov02c10_csi_on_trans_finished(esp_cam_ctlr_handle_t handle,
                                                     esp_cam_ctlr_trans_t *trans,
                                                     void *user_data)
{
    (void)handle;
    (void)trans;
    (void)user_data;
    return false;
}

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

#if CONFIG_ESP_ISP_ENABLE
static uint32_t ov02c10_clamp_u32(uint32_t value, uint32_t min_val, uint32_t max_val)
{
    if (value < min_val) {
        return min_val;
    }
    if (value > max_val) {
        return max_val;
    }
    return value;
}

static esp_err_t ov02c10_write_exposure_registers(uint16_t exposure)
{
    esp_err_t ret = ov02c10_write_reg8(OV02C10_REG_EXPO_H, (uint8_t)((exposure >> 8) & 0xFFU));
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to write OV02C10 exposure high register");
    ret = ov02c10_write_reg8(OV02C10_REG_EXPO_L, (uint8_t)(exposure & 0xFFU));
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to write OV02C10 exposure low register");
    return ESP_OK;
}

static IRAM_ATTR bool ov02c10_ae_on_statistics_done(isp_ae_ctlr_t ae_ctlr,
                                                    const esp_isp_ae_env_detector_evt_data_t *edata,
                                                    void *user_data)
{
    (void)ae_ctlr;
    (void)user_data;
    if (edata == NULL) {
        return false;
    }

    uint64_t luma_sum = 0;
    for (int x = 0; x < ISP_AE_BLOCK_X_NUM; x++) {
        for (int y = 0; y < ISP_AE_BLOCK_Y_NUM; y++) {
            luma_sum += (uint32_t)edata->ae_result.luminance[x][y];
        }
    }

    uint32_t block_num = ISP_AE_BLOCK_X_NUM * ISP_AE_BLOCK_Y_NUM;
    if (block_num == 0) {
        return false;
    }

    uint32_t avg_luma = (uint32_t)(luma_sum / block_num);

    portENTER_CRITICAL_ISR(&s_isp_lock);
    uint32_t exposure = s_ae_exposure_reg;
    if (avg_luma + OV02C10_ISP_AE_HYSTERESIS < OV02C10_ISP_AE_TARGET_LUMA) {
        exposure += (exposure >> 4) + 1U;
    } else if (avg_luma > OV02C10_ISP_AE_TARGET_LUMA + OV02C10_ISP_AE_HYSTERESIS) {
        uint32_t dec = (exposure >> 4) + 1U;
        exposure = (exposure > dec) ? (exposure - dec) : OV02C10_ISP_AE_EXPO_MIN;
    }

    exposure = ov02c10_clamp_u32(exposure, OV02C10_ISP_AE_EXPO_MIN, OV02C10_ISP_AE_EXPO_MAX);
    s_ae_exposure_reg = (uint16_t)exposure;
    s_ae_update_pending = true;
    portEXIT_CRITICAL_ISR(&s_isp_lock);

    return false;
}

static IRAM_ATTR bool ov02c10_awb_on_statistics_done(isp_awb_ctlr_t awb_ctlr,
                                                     const esp_isp_awb_evt_data_t *edata,
                                                     void *user_data)
{
    (void)awb_ctlr;
    (void)user_data;
    if (edata == NULL) {
        return false;
    }

    uint32_t sum_r = edata->awb_result.sum_r;
    uint32_t sum_g = edata->awb_result.sum_g;
    uint32_t sum_b = edata->awb_result.sum_b;
    if (sum_r == 0 || sum_g == 0 || sum_b == 0) {
        return false;
    }

    uint32_t gain_r = (sum_g * OV02C10_ISP_AWB_GAIN_BASE) / sum_r;
    uint32_t gain_b = (sum_g * OV02C10_ISP_AWB_GAIN_BASE) / sum_b;
    gain_r = ov02c10_clamp_u32(gain_r, OV02C10_ISP_AWB_GAIN_MIN, OV02C10_ISP_AWB_GAIN_MAX);
    gain_b = ov02c10_clamp_u32(gain_b, OV02C10_ISP_AWB_GAIN_MIN, OV02C10_ISP_AWB_GAIN_MAX);

    portENTER_CRITICAL_ISR(&s_isp_lock);
    s_awb_gain_pending.gain_r = gain_r;
    s_awb_gain_pending.gain_g = OV02C10_ISP_AWB_GAIN_BASE;
    s_awb_gain_pending.gain_b = gain_b;
    s_awb_update_pending = true;
    portEXIT_CRITICAL_ISR(&s_isp_lock);

    return false;
}

static void ov02c10_isp_control_task(void *arg)
{
    (void)arg;
    while (s_isp_task_running) {
        bool ae_pending = false;
        bool awb_pending = false;
        uint16_t exposure = 0;
        isp_wbg_gain_t gain = {0};

        portENTER_CRITICAL(&s_isp_lock);
        ae_pending = s_ae_update_pending;
        awb_pending = s_awb_update_pending;
        exposure = s_ae_exposure_reg;
        gain = s_awb_gain_pending;
        s_ae_update_pending = false;
        s_awb_update_pending = false;
        portEXIT_CRITICAL(&s_isp_lock);

        if (ae_pending) {
            (void)ov02c10_write_exposure_registers(exposure);
        }

        if (awb_pending && s_isp_wbg_enabled && s_isp_proc) {
            (void)esp_isp_wbg_set_wb_gain(s_isp_proc, gain);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    s_isp_ctrl_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t ov02c10_start_isp_control_task(void)
{
    if (s_isp_ctrl_task) {
        return ESP_OK;
    }

    s_isp_task_running = true;
    BaseType_t ok = xTaskCreate(
        ov02c10_isp_control_task,
        "ov02_isp_ctrl",
        OV02C10_ISP_TASK_STACK,
        NULL,
        OV02C10_ISP_TASK_PRIO,
        &s_isp_ctrl_task);

    if (ok != pdPASS) {
        s_isp_task_running = false;
        s_isp_ctrl_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void ov02c10_stop_isp_control_task(void)
{
    if (s_isp_ctrl_task) {
        s_isp_task_running = false;
        vTaskDelete(s_isp_ctrl_task);
        s_isp_ctrl_task = NULL;
    }
}

static void ov02c10_release_isp_pipeline(void)
{
#if CONFIG_ESP_ISP_AUTO_AWB
    if (s_isp_awb_ctlr) {
        (void)esp_isp_awb_controller_stop_continuous_statistics(s_isp_awb_ctlr);
        (void)esp_isp_awb_controller_disable(s_isp_awb_ctlr);
        (void)esp_isp_del_awb_controller(s_isp_awb_ctlr);
        s_isp_awb_ctlr = NULL;
    }
#endif

#if CONFIG_ESP_ISP_AUTO_AE
    if (s_isp_ae_ctlr) {
        (void)esp_isp_ae_controller_stop_continuous_statistics(s_isp_ae_ctlr);
        (void)esp_isp_ae_controller_disable(s_isp_ae_ctlr);
        (void)esp_isp_del_ae_controller(s_isp_ae_ctlr);
        s_isp_ae_ctlr = NULL;
    }
#endif

    ov02c10_stop_isp_control_task();

    if (s_isp_wbg_enabled && s_isp_proc) {
        (void)esp_isp_wbg_disable(s_isp_proc);
        s_isp_wbg_enabled = false;
    }

    if (s_isp_blc_enabled && s_isp_proc) {
        (void)esp_isp_blc_disable(s_isp_proc);
        s_isp_blc_enabled = false;
    }

    if (s_isp_ccm_enabled && s_isp_proc) {
        (void)esp_isp_ccm_disable(s_isp_proc);
        s_isp_ccm_enabled = false;
    }

    if (s_isp_demosaic_enabled && s_isp_proc) {
        (void)esp_isp_demosaic_disable(s_isp_proc);
        s_isp_demosaic_enabled = false;
    }

    if (s_isp_proc) {
        (void)esp_isp_disable(s_isp_proc);
        (void)esp_isp_del_processor(s_isp_proc);
        s_isp_proc = NULL;
    }

    if (s_isp_rgb888_frame) {
        heap_caps_free(s_isp_rgb888_frame);
        s_isp_rgb888_frame = NULL;
        s_isp_rgb888_frame_size = 0;
    }

    s_ae_exposure_reg = OV02C10_ISP_AE_EXPO_DEFAULT;
    s_ae_update_pending = false;
    s_awb_update_pending = false;
    s_awb_gain_pending.gain_r = OV02C10_ISP_AWB_GAIN_BASE;
    s_awb_gain_pending.gain_g = OV02C10_ISP_AWB_GAIN_BASE;
    s_awb_gain_pending.gain_b = OV02C10_ISP_AWB_GAIN_BASE;
}
#endif

static esp_err_t ov02c10_enable_mclk_24mhz(void)
{
    if (s_mclk_running) {
        return ESP_OK;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = OV02C10_MCLK_LEDC_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_1_BIT,
        .timer_num = OV02C10_MCLK_LEDC_TIMER,
        .freq_hz = OV02C10_MCLK_HZ,
        .clk_cfg = OV02C10_MCLK_LEDC_CLK_CFG,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to configure OV02C10 MCLK timer");

    ledc_channel_config_t chan_cfg = {
        .gpio_num = CONFIG_BSP_PIN_CAMERA_MCLK,
        .speed_mode = OV02C10_MCLK_LEDC_SPEED_MODE,
        .channel = OV02C10_MCLK_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = OV02C10_MCLK_LEDC_TIMER,
        .duty = 1,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&chan_cfg);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to route OV02C10 MCLK to GPIO");

    /* 8192 cycles @24MHz ~= 341us; wait 1ms for safe margin before SCCB ops. */
    (void)OV02C10_MCLK_STABLE_CYCLES;
    vTaskDelay(pdMS_TO_TICKS(1));

    s_mclk_running = true;
    LOGI("MCLK running on GPIO %d @ %u Hz (LEDC mode=%d timer=%d channel=%d)",
         CONFIG_BSP_PIN_CAMERA_MCLK,
         OV02C10_MCLK_HZ,
         (int)OV02C10_MCLK_LEDC_SPEED_MODE,
         (int)OV02C10_MCLK_LEDC_TIMER,
         (int)OV02C10_MCLK_LEDC_CHANNEL);
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

    const esp_cam_ctlr_evt_cbs_t csi_cbs = {
        .on_get_new_trans = NULL,
        .on_trans_finished = ov02c10_csi_on_trans_finished,
    };
    ret = esp_cam_ctlr_register_event_callbacks(s_csi_ctlr, &csi_cbs, NULL);
    if (ret != ESP_OK) {
        esp_cam_ctlr_del(s_csi_ctlr);
        s_csi_ctlr = NULL;
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to register CSI callbacks");
    }

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

    LOGI("[OV02C10] Sensor detected. ID: 0x%06" PRIX32 ".", chip_id);
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
        .output_data_color_type = ISP_COLOR_RGB888,
        .yuv_range = ISP_COLOR_RANGE_FULL,
        .yuv_std = ISP_YUV_CONV_STD_BT601,
        .has_line_start_packet = false,
        .has_line_end_packet = false,
        .h_res = CONFIG_BSP_CAMERA_FRAME_WIDTH,
        .v_res = CONFIG_BSP_CAMERA_FRAME_HEIGHT,
        .bayer_order = COLOR_RAW_ELEMENT_ORDER_BGGR,
        .intr_priority = 0,
        .flags = {
            .bypass_isp = 0,
            .byte_swap_en = 0,
        },
    };

    esp_err_t ret = esp_isp_new_processor(&isp_cfg, &s_isp_proc);
    if (ret != ESP_OK) {
        LOGE("Failed to create ISP processor: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_isp_enable(s_isp_proc);
    if (ret != ESP_OK) {
        LOGE("Failed to enable ISP processor: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }

    esp_isp_demosaic_config_t demosaic_cfg = {
        .grad_ratio = {.val = (1U << ISP_DEMOSAIC_GRAD_RATIO_DEC_BITS)},
        .padding_mode = ISP_DEMOSAIC_EDGE_PADDING_MODE_SRND_DATA,
        .padding_data = 0,
        .padding_line_tail_valid_start_pixel = 0,
        .padding_line_tail_valid_end_pixel = 0,
    };
    ret = esp_isp_demosaic_configure(s_isp_proc, &demosaic_cfg);
    if (ret != ESP_OK) {
        LOGE("Failed to configure ISP demosaic: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }
    ret = esp_isp_demosaic_enable(s_isp_proc);
    if (ret != ESP_OK) {
        LOGE("Failed to enable ISP demosaic: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }
    s_isp_demosaic_enabled = true;

    esp_isp_ccm_config_t ccm_cfg = {
        .matrix = {
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
        },
        .saturation = true,
        .flags = {
            .update_once_configured = 1,
        },
    };
    ret = esp_isp_ccm_configure(s_isp_proc, &ccm_cfg);
    if (ret != ESP_OK) {
        LOGE("Failed to configure ISP CCM: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }
    ret = esp_isp_ccm_enable(s_isp_proc);
    if (ret != ESP_OK) {
        LOGE("Failed to enable ISP CCM: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }
    s_isp_ccm_enabled = true;

    esp_isp_blc_config_t blc_cfg = {
        .window = {
            .top_left = {.x = 0, .y = 0},
            .btm_right = {.x = CONFIG_BSP_CAMERA_FRAME_WIDTH, .y = CONFIG_BSP_CAMERA_FRAME_HEIGHT},
        },
        .filter_threshold = {
            .top_left_chan_thresh = 16,
            .top_right_chan_thresh = 16,
            .bottom_left_chan_thresh = 16,
            .bottom_right_chan_thresh = 16,
        },
        .filter_enable = false,
        .stretch = {
            .top_left_chan_stretch_en = false,
            .top_right_chan_stretch_en = false,
            .bottom_left_chan_stretch_en = false,
            .bottom_right_chan_stretch_en = false,
        },
        .flags = {
            .update_once_configured = 1,
        },
    };
    ret = esp_isp_blc_configure(s_isp_proc, &blc_cfg);
    if (ret == ESP_OK) {
        ret = esp_isp_blc_enable(s_isp_proc);
        if (ret != ESP_OK) {
            LOGE("Failed to enable ISP BLC: %s", esp_err_to_name(ret));
            ov02c10_release_isp_pipeline();
            return ret;
        }
        s_isp_blc_enabled = true;
    } else if (ret == ESP_ERR_NOT_SUPPORTED) {
        LOGW("ISP BLC not supported on this silicon revision, continue without BLC");
    } else {
        LOGE("Failed to configure ISP BLC: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }

    esp_isp_wbg_config_t wbg_cfg = {
        .flags = {
            .update_once_configured = 1,
        },
    };
    ret = esp_isp_wbg_configure(s_isp_proc, &wbg_cfg);
    if (ret == ESP_OK) {
        ret = esp_isp_wbg_enable(s_isp_proc);
        if (ret != ESP_OK) {
            LOGE("Failed to enable ISP WBG: %s", esp_err_to_name(ret));
            ov02c10_release_isp_pipeline();
            return ret;
        }

        isp_wbg_gain_t init_gain = {
            .gain_r = OV02C10_ISP_AWB_GAIN_BASE,
            .gain_g = OV02C10_ISP_AWB_GAIN_BASE,
            .gain_b = OV02C10_ISP_AWB_GAIN_BASE,
        };
        (void)esp_isp_wbg_set_wb_gain(s_isp_proc, init_gain);
        s_isp_wbg_enabled = true;
    } else if (ret == ESP_ERR_NOT_SUPPORTED) {
        LOGW("ISP WBG not supported on this silicon revision, AWB gain updates disabled");
    } else {
        LOGE("Failed to configure ISP WBG: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }

#if CONFIG_ESP_ISP_AUTO_AE
    esp_isp_ae_config_t ae_cfg = {
        .sample_point = ISP_AE_SAMPLE_POINT_AFTER_DEMOSAIC,
        .window = {
            .top_left = {.x = 0, .y = 0},
            .btm_right = {.x = CONFIG_BSP_CAMERA_FRAME_WIDTH, .y = CONFIG_BSP_CAMERA_FRAME_HEIGHT},
        },
        .intr_priority = 0,
    };
    ret = esp_isp_new_ae_controller(s_isp_proc, &ae_cfg, &s_isp_ae_ctlr);
    if (ret != ESP_OK) {
        LOGE("Failed to create ISP AE controller: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }

    esp_isp_ae_env_detector_evt_cbs_t ae_cbs = {
        .on_env_statistics_done = ov02c10_ae_on_statistics_done,
        .on_env_change = ov02c10_ae_on_statistics_done,
    };
    ret = esp_isp_ae_env_detector_register_event_callbacks(s_isp_ae_ctlr, &ae_cbs, NULL);
    if (ret != ESP_OK) {
        LOGE("Failed to register ISP AE callbacks: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }

    ret = esp_isp_ae_controller_enable(s_isp_ae_ctlr);
    if (ret != ESP_OK) {
        LOGE("Failed to enable ISP AE controller: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }

    ret = esp_isp_ae_controller_start_continuous_statistics(s_isp_ae_ctlr);
    if (ret != ESP_OK) {
        LOGE("Failed to start ISP AE statistics: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }
#endif

#if CONFIG_ESP_ISP_AUTO_AWB
    esp_isp_awb_config_t awb_cfg = {
        .sample_point = ISP_AWB_SAMPLE_POINT_AFTER_CCM,
        .window = {
            .top_left = {.x = CONFIG_BSP_CAMERA_FRAME_WIDTH / 8U, .y = CONFIG_BSP_CAMERA_FRAME_HEIGHT / 8U},
            .btm_right = {.x = (CONFIG_BSP_CAMERA_FRAME_WIDTH * 7U) / 8U, .y = (CONFIG_BSP_CAMERA_FRAME_HEIGHT * 7U) / 8U},
        },
        .subwindow = {
            .top_left = {.x = 0, .y = 0},
            .btm_right = {.x = 0, .y = 0},
        },
        .white_patch = {
            .luminance = {.min = 0, .max = 220 * 3},
            .red_green_ratio = {.min = 0.0f, .max = 3.999f},
            .blue_green_ratio = {.min = 0.0f, .max = 3.999f},
        },
        .intr_priority = 0,
    };
    ret = esp_isp_new_awb_controller(s_isp_proc, &awb_cfg, &s_isp_awb_ctlr);
    if (ret != ESP_OK) {
        LOGE("Failed to create ISP AWB controller: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }

    esp_isp_awb_cbs_t awb_cbs = {
        .on_statistics_done = ov02c10_awb_on_statistics_done,
    };
    ret = esp_isp_awb_register_event_callbacks(s_isp_awb_ctlr, &awb_cbs, NULL);
    if (ret != ESP_OK) {
        LOGE("Failed to register ISP AWB callbacks: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }

    ret = esp_isp_awb_controller_enable(s_isp_awb_ctlr);
    if (ret != ESP_OK) {
        LOGE("Failed to enable ISP AWB controller: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }

    ret = esp_isp_awb_controller_start_continuous_statistics(s_isp_awb_ctlr);
    if (ret != ESP_OK) {
        LOGE("Failed to start ISP AWB statistics: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }
#endif

    s_isp_rgb888_frame_size = (size_t)CONFIG_BSP_CAMERA_FRAME_WIDTH *
                              (size_t)CONFIG_BSP_CAMERA_FRAME_HEIGHT * 3U;
    s_isp_rgb888_frame = heap_caps_malloc(s_isp_rgb888_frame_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_isp_rgb888_frame == NULL) {
        LOGW("Failed to allocate RGB888 frame buffer in PSRAM (%u bytes)", (unsigned int)s_isp_rgb888_frame_size);
    } else {
        LOGI("ISP RGB888 output buffer allocated in PSRAM (%u bytes)", (unsigned int)s_isp_rgb888_frame_size);
    }

    ret = ov02c10_start_isp_control_task();
    if (ret != ESP_OK) {
        LOGE("Failed to start ISP control task: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }

    LOGI("ISP pipeline ready (RAW10 input, RGB888 output, AE=%d, AWB=%d)",
         (int)CONFIG_ESP_ISP_AUTO_AE,
         (int)CONFIG_ESP_ISP_AUTO_AWB);
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
    ov02c10_release_isp_pipeline();
#endif

    if (s_cam_dev_handle) {
        i2c_master_bus_rm_device(s_cam_dev_handle);
        s_cam_dev_handle = NULL;
    }

    if (s_mclk_running) {
        ledc_stop(OV02C10_MCLK_LEDC_SPEED_MODE, OV02C10_MCLK_LEDC_CHANNEL, 0);
    }

    s_streaming = false;
    s_initialized = false;
    s_powered = false;
    s_mclk_running = false;
}
