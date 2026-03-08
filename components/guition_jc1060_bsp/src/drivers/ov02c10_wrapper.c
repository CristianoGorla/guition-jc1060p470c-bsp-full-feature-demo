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
#include "esp_cam_sensor.h"
#include "esp_check.h"
#include "esp_sccb_i2c.h"
#include "esp_sccb_intf.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ov02c10.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#if CONFIG_ESP_ISP_ENABLE
#include "driver/isp.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#endif

#if CONFIG_ESP_PPA_ENABLE
#include "driver/ppa.h"
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

#ifndef CONFIG_BSP_CAMERA_DEBUG_SENSOR_TEST_PATTERN
#define CONFIG_BSP_CAMERA_DEBUG_SENSOR_TEST_PATTERN 0
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
#define OV02C10_CACHE_LINE_SIZE         128U

/*
 * ESP32-P4 rev < 3.0: keep ISP auto-controls disabled to avoid unstable
 * behavior/artifacts on early silicon.
 */
#if CONFIG_BSP_ESP32P4_SILICON_REV_LOWER_3
#define OV02C10_ISP_AUTO_AE_ENABLE      0
#define OV02C10_ISP_AUTO_AWB_ENABLE     0
#else
#define OV02C10_ISP_AUTO_AE_ENABLE      CONFIG_ESP_ISP_AUTO_AE
#define OV02C10_ISP_AUTO_AWB_ENABLE     CONFIG_ESP_ISP_AUTO_AWB
#endif

#define OV02C10_PREVIEW_WIDTH           1024U
#define OV02C10_PREVIEW_HEIGHT          600U
#define OV02C10_PPA_SRC_WIDTH_PIXELS    1920U
#define OV02C10_PPA_SRC_HEIGHT_PIXELS   1080U
#define OV02C10_PPA_SRC_STRIDE_BYTES    5760U
#define OV02C10_PPA_DST_STRIDE_BYTES    2048U
#define OV02C10_PREVIEW_FRAME_BYTES     (OV02C10_PPA_DST_STRIDE_BYTES * OV02C10_PREVIEW_HEIGHT)
#define OV02C10_PPA_CROP_OFFSET_X       ((OV02C10_PPA_SRC_WIDTH_PIXELS - OV02C10_PREVIEW_WIDTH) / 2U)
#define OV02C10_PPA_CROP_OFFSET_Y       ((OV02C10_PPA_SRC_HEIGHT_PIXELS - OV02C10_PREVIEW_HEIGHT) / 2U)
#if (OV02C10_PPA_SRC_STRIDE_BYTES != (OV02C10_PPA_SRC_WIDTH_PIXELS * 3U))
#error "OV02C10 source stride must be 1920*3 = 5760 bytes"
#endif
#if (OV02C10_PPA_DST_STRIDE_BYTES != (OV02C10_PREVIEW_WIDTH * 2U))
#error "OV02C10 destination stride must be 1024*2 = 2048 bytes"
#endif
#define OV02C10_PREVIEW_TASK_STACK      6144
#define OV02C10_PREVIEW_TASK_PRIO       6

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
static esp_sccb_io_handle_t s_cam_sccb_handle = NULL;
static esp_cam_sensor_device_t *s_cam_sensor = NULL;
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
#if CONFIG_ESP_PPA_ENABLE
static ppa_client_handle_t s_ppa_srm_client = NULL;
static TaskHandle_t s_preview_task = NULL;
static bool s_preview_task_running = false;
static bool s_preview_enabled = false;
static void *s_preview_frames[2] = {NULL, NULL};
static size_t s_preview_frame_size = 0;
static uint8_t s_preview_front_idx = 0;
static bsp_camera_preview_frame_cb_t s_preview_frame_cb = NULL;
static void *s_preview_frame_cb_user_data = NULL;
#endif
#endif

#if CONFIG_ESP_ISP_ENABLE && CONFIG_ESP_PPA_ENABLE
static IRAM_ATTR bool ov02c10_csi_on_get_new_trans(esp_cam_ctlr_handle_t handle,
                                                    esp_cam_ctlr_trans_t *trans,
                                                    void *user_data)
{
    (void)handle;
    (void)user_data;

    if (trans == NULL) {
        return false;
    }

    if (s_isp_rgb888_frame && s_isp_rgb888_frame_size > 0) {
        trans->buffer = s_isp_rgb888_frame;
        trans->buflen = s_isp_rgb888_frame_size;
    }

    return false;
}
#endif

static IRAM_ATTR bool ov02c10_csi_on_trans_finished(esp_cam_ctlr_handle_t handle,
                                                     esp_cam_ctlr_trans_t *trans,
                                                     void *user_data)
{
    (void)handle;
    (void)trans;
    (void)user_data;
#if CONFIG_ESP_ISP_ENABLE && CONFIG_ESP_PPA_ENABLE
    if (s_preview_enabled && s_preview_task) {
        BaseType_t hp_task_woken = pdFALSE;
        vTaskNotifyGiveFromISR(s_preview_task, &hp_task_woken);
        return hp_task_woken == pdTRUE;
    }
#endif
    return false;
}

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

    if (s_cam_sccb_handle != NULL) {
        return ESP_OK;
    }

    sccb_i2c_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ov02c10_sccb_addr_7bit(),
        .scl_speed_hz = CONFIG_BSP_I2C_FREQ_HZ,
    };

    esp_err_t ret = sccb_new_i2c_io(i2c_bus, &dev_cfg, &s_cam_sccb_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create OV02C10 SCCB interface");

    LOGI("SCCB device ready (cfg addr=0x%02X, 7-bit=0x%02X)",
         (unsigned int)CONFIG_BSP_CAMERA_I2C_ADDR,
         (unsigned int)ov02c10_sccb_addr_7bit());
    return ESP_OK;
}

static esp_err_t ov02c10_detect_sensor(void)
{
    if (s_cam_sensor != NULL) {
        return ESP_OK;
    }

    if (s_cam_sccb_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_cam_sensor_config_t cam_cfg = {
        .sccb_handle = s_cam_sccb_handle,
        .reset_pin = -1,
        .pwdn_pin = -1,
        .xclk_pin = -1,
        .xclk_freq_hz = (int32_t)OV02C10_MCLK_HZ,
        .sensor_port = ESP_CAM_SENSOR_MIPI_CSI,
    };

    s_cam_sensor = ov02c10_detect(&cam_cfg);
    if (s_cam_sensor == NULL) {
        LOGE("esp_cam_sensor failed to detect OV02C10 on SCCB 0x%02X", (unsigned int)ov02c10_sccb_addr_7bit());
        return ESP_ERR_NOT_FOUND;
    }

    LOGI("esp_cam_sensor detected camera: %s", esp_cam_sensor_get_name(s_cam_sensor));
    return ESP_OK;
}

static esp_err_t ov02c10_write_reg8(uint16_t reg, uint8_t value)
{
    if (s_cam_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_cam_sensor_reg_val_t reg_val = {
        .regaddr = reg,
        .value = value,
    };
    return esp_cam_sensor_ioctl(s_cam_sensor, ESP_CAM_SENSOR_IOC_S_REG, &reg_val);
}

static esp_err_t ov02c10_set_sensor_stream_mode(bool enable)
{
    if (s_cam_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int stream_on = enable ? 1 : 0;
    return esp_cam_sensor_ioctl(s_cam_sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_on);
}

static esp_err_t ov02c10_set_sensor_test_pattern_mode(bool enable)
{
    if (s_cam_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int test_pattern = enable ? 1 : 0;
    return esp_cam_sensor_ioctl(s_cam_sensor, ESP_CAM_SENSOR_IOC_S_TEST_PATTERN, &test_pattern);
}

static esp_err_t ov02c10_apply_manual_brightness_boost(void)
{
    if (s_cam_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * On ESP32-P4 early silicon (rev < v3.0) ISP auto AE/AWB are disabled.
     * Apply a conservative manual exposure/gain boost to avoid underexposed preview.
     */
    esp_err_t ret;
    esp_cam_sensor_param_desc_t gain_desc = {
        .id = ESP_CAM_SENSOR_GAIN,
    };
    esp_cam_sensor_param_desc_t exp_desc = {
        .id = ESP_CAM_SENSOR_EXPOSURE_VAL,
    };

    ret = esp_cam_sensor_query_para_desc(s_cam_sensor, &gain_desc);
    if (ret == ESP_OK &&
        gain_desc.type == ESP_CAM_SENSOR_PARAM_TYPE_ENUMERATION &&
        gain_desc.enumeration.count > 0) {
        const uint32_t requested_gain_idx = 160U;
        uint32_t gain_idx = requested_gain_idx;
        uint32_t max_idx = gain_desc.enumeration.count - 1U;
        if (gain_idx > max_idx) {
            gain_idx = max_idx;
        }

        ret = esp_cam_sensor_set_para_value(s_cam_sensor,
                                            ESP_CAM_SENSOR_GAIN,
                                            &gain_idx,
                                            sizeof(gain_idx));
        if (ret == ESP_OK) {
            LOGW("[OV02C10] Manual gain boost applied: idx=%u", (unsigned int)gain_idx);
        } else {
            LOGW("[OV02C10] Failed to set manual gain boost: %s", esp_err_to_name(ret));
        }
    } else {
        LOGW("[OV02C10] Gain descriptor unavailable, skip manual gain boost");
    }

    ret = esp_cam_sensor_query_para_desc(s_cam_sensor, &exp_desc);
    if (ret == ESP_OK && exp_desc.type == ESP_CAM_SENSOR_PARAM_TYPE_NUMBER) {
        const uint32_t requested_exposure = 0x0900U;
        int32_t min_exposure = exp_desc.number.minimum;
        int32_t max_exposure = exp_desc.number.maximum;

        uint32_t exposure = requested_exposure;
        if ((int32_t)exposure < min_exposure) {
            exposure = (uint32_t)min_exposure;
        }
        if ((int32_t)exposure > max_exposure) {
            exposure = (uint32_t)max_exposure;
        }

        ret = esp_cam_sensor_set_para_value(s_cam_sensor,
                                            ESP_CAM_SENSOR_EXPOSURE_VAL,
                                            &exposure,
                                            sizeof(exposure));
        if (ret == ESP_OK) {
            LOGW("[OV02C10] Manual exposure boost applied: 0x%04X", (unsigned int)exposure);
        } else {
            LOGW("[OV02C10] Failed to set manual exposure boost: %s", esp_err_to_name(ret));
        }
    } else {
        LOGW("[OV02C10] Exposure descriptor unavailable, skip manual exposure boost");
    }

    return ESP_OK;
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

#if CONFIG_BSP_ESP32P4_SILICON_REV_LOWER_3
        (void)awb_pending;
        (void)gain;
#else
        if (awb_pending && s_isp_wbg_enabled && s_isp_proc) {
            (void)esp_isp_wbg_set_wb_gain(s_isp_proc, gain);
        }
#endif

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

#if CONFIG_ESP_PPA_ENABLE
static esp_err_t ov02c10_init_ppa_scaler(void)
{
    if (s_ppa_srm_client) {
        return ESP_OK;
    }

    ppa_client_config_t client_cfg = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1,
        .data_burst_length = PPA_DATA_BURST_LENGTH_16,
    };

    esp_err_t ret = ppa_register_client(&client_cfg, &s_ppa_srm_client);
    if (ret != ESP_OK) {
        LOGE("Failed to register PPA SRM client: %s", esp_err_to_name(ret));
        return ret;
    }

    s_preview_frame_size = (size_t)OV02C10_PREVIEW_FRAME_BYTES;
    s_preview_front_idx = 0;
    for (int i = 0; i < 2; i++) {
        s_preview_frames[i] = heap_caps_aligned_alloc(128,
                                                      s_preview_frame_size,
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_preview_frames[i] == NULL) {
            LOGE("Failed to allocate preview RGB buffer[%d] in PSRAM (%u bytes)",
                 i,
                 (unsigned int)s_preview_frame_size);
            for (int j = 0; j < 2; j++) {
                if (s_preview_frames[j]) {
                    heap_caps_free(s_preview_frames[j]);
                    s_preview_frames[j] = NULL;
                }
            }
            (void)ppa_unregister_client(s_ppa_srm_client);
            s_ppa_srm_client = NULL;
            s_preview_frame_size = 0;
            return ESP_ERR_NO_MEM;
        }
    }

    LOGI("[OV02C10] PPA Scaler initialized: 1080p -> 1024x600.");
    return ESP_OK;
}

static void ov02c10_deinit_ppa_scaler(void)
{
    if (s_ppa_srm_client) {
        (void)ppa_unregister_client(s_ppa_srm_client);
        s_ppa_srm_client = NULL;
    }

    for (int i = 0; i < 2; i++) {
        if (s_preview_frames[i]) {
            heap_caps_free(s_preview_frames[i]);
            s_preview_frames[i] = NULL;
        }
    }
    s_preview_frame_size = 0;
    s_preview_front_idx = 0;
}

static void task_camera_preview(void *arg)
{
    (void)arg;

    while (s_preview_task_running) {
        uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        if (notified == 0) {
            continue;
        }

        if (!s_preview_enabled || !s_ppa_srm_client || !s_isp_rgb888_frame || !s_preview_frames[0] || !s_preview_frames[1]) {
            continue;
        }

        uint8_t back_idx;
        portENTER_CRITICAL(&s_isp_lock);
        back_idx = (uint8_t)(s_preview_front_idx ^ 1U);
        portEXIT_CRITICAL(&s_isp_lock);

        void *dst_buf = s_preview_frames[back_idx];

        ppa_srm_oper_config_t oper_cfg = {
            .in = {
                .buffer = s_isp_rgb888_frame,
                .pic_w = OV02C10_PPA_SRC_WIDTH_PIXELS,
                .pic_h = OV02C10_PPA_SRC_HEIGHT_PIXELS,
                .block_w = OV02C10_PREVIEW_WIDTH,
                .block_h = OV02C10_PREVIEW_HEIGHT,
                .block_offset_x = OV02C10_PPA_CROP_OFFSET_X,
                .block_offset_y = OV02C10_PPA_CROP_OFFSET_Y,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB888,
                .yuv_range = PPA_COLOR_RANGE_FULL,
                .yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601,
            },
            .out = {
                .buffer = dst_buf,
                .buffer_size = s_preview_frame_size,
                .pic_w = OV02C10_PREVIEW_WIDTH,
                .pic_h = OV02C10_PREVIEW_HEIGHT,
                .block_offset_x = 0,
                .block_offset_y = 0,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
                .yuv_range = PPA_COLOR_RANGE_FULL,
                .yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601,
            },
            .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
            .scale_x = 1.0f,
            .scale_y = 1.0f,
            .mirror_x = false,
            .mirror_y = false,
            .rgb_swap = false,
            .byte_swap = false,
            .alpha_update_mode = PPA_ALPHA_NO_CHANGE,
            .mode = PPA_TRANS_MODE_BLOCKING,
            .user_data = NULL,
        };

        esp_err_t ret = ppa_do_scale_rotate_mirror(s_ppa_srm_client, &oper_cfg);
        if (ret != ESP_OK) {
            LOGW("PPA scale failed: %s", esp_err_to_name(ret));
            continue;
        }

        (void)esp_cache_msync(dst_buf,
                              OV02C10_PREVIEW_FRAME_BYTES,
                              ESP_CACHE_MSYNC_FLAG_DIR_M2C);

        portENTER_CRITICAL(&s_isp_lock);
        s_preview_front_idx = back_idx;
        portEXIT_CRITICAL(&s_isp_lock);

        if (s_preview_frame_cb) {
            s_preview_frame_cb(s_preview_frame_cb_user_data);
        }
    }

    s_preview_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t ov02c10_start_preview_task(void)
{
    if (s_preview_task) {
        return ESP_OK;
    }

    s_preview_task_running = true;
    BaseType_t ok = xTaskCreatePinnedToCore(task_camera_preview,
                                            "task_camera_preview",
                                            OV02C10_PREVIEW_TASK_STACK,
                                            NULL,
                                            OV02C10_PREVIEW_TASK_PRIO,
                                            &s_preview_task,
                                            1);
    if (ok != pdPASS) {
        s_preview_task_running = false;
        s_preview_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    LOGI("[OV02C10] Preview task started on Core 1.");
    return ESP_OK;
}

static void ov02c10_stop_preview_task(void)
{
    if (s_preview_task) {
        s_preview_task_running = false;
        vTaskDelete(s_preview_task);
        s_preview_task = NULL;
    }
}
#endif

static void ov02c10_release_isp_pipeline(void)
{
#if OV02C10_ISP_AUTO_AWB_ENABLE
    if (s_isp_awb_ctlr) {
        (void)esp_isp_awb_controller_stop_continuous_statistics(s_isp_awb_ctlr);
        (void)esp_isp_awb_controller_disable(s_isp_awb_ctlr);
        (void)esp_isp_del_awb_controller(s_isp_awb_ctlr);
        s_isp_awb_ctlr = NULL;
    }
#endif

#if OV02C10_ISP_AUTO_AE_ENABLE
    if (s_isp_ae_ctlr) {
        (void)esp_isp_ae_controller_stop_continuous_statistics(s_isp_ae_ctlr);
        (void)esp_isp_ae_controller_disable(s_isp_ae_ctlr);
        (void)esp_isp_del_ae_controller(s_isp_ae_ctlr);
        s_isp_ae_ctlr = NULL;
    }
#endif

#if CONFIG_ESP_PPA_ENABLE
    s_preview_enabled = false;
    s_preview_frame_cb = NULL;
    s_preview_frame_cb_user_data = NULL;
    ov02c10_stop_preview_task();
#endif

    ov02c10_stop_isp_control_task();

#if !CONFIG_BSP_ESP32P4_SILICON_REV_LOWER_3
    if (s_isp_wbg_enabled && s_isp_proc) {
        (void)esp_isp_wbg_disable(s_isp_proc);
        s_isp_wbg_enabled = false;
    }

    if (s_isp_blc_enabled && s_isp_proc) {
        (void)esp_isp_blc_disable(s_isp_proc);
        s_isp_blc_enabled = false;
    }
#endif

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

#if CONFIG_ESP_PPA_ENABLE
    ov02c10_deinit_ppa_scaler();
#endif

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
        .output_data_color_type = CAM_CTLR_COLOR_RGB888,
        .queue_items = 1,
        .byte_swap_en = 0,
        .bk_buffer_dis = 0,
    };

    esp_err_t ret = esp_cam_new_csi_ctlr(&csi_cfg, &s_csi_ctlr);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create CSI controller");

    const esp_cam_ctlr_evt_cbs_t csi_cbs = {
        .on_get_new_trans =
#if CONFIG_ESP_ISP_ENABLE && CONFIG_ESP_PPA_ENABLE
            ov02c10_csi_on_get_new_trans,
#else
            NULL,
#endif
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
    if (s_cam_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_cam_sensor_set_format(s_cam_sensor, NULL);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to load OV02C10 default format from esp_cam_sensor");

    esp_cam_sensor_format_t format = {0};
    if (esp_cam_sensor_get_format(s_cam_sensor, &format) == ESP_OK) {
        LOGI("OV02C10 format loaded: %s (%ux%u, lanes=%u, fps=%u)",
             format.name ? format.name : "unknown",
             (unsigned int)format.width,
             (unsigned int)format.height,
             (unsigned int)format.mipi_info.lane_num,
             (unsigned int)format.fps);

        if (format.format != ESP_CAM_SENSOR_PIXFORMAT_RAW10) {
            LOGE("Unsupported sensor format for ISP pipeline: %d (expected RAW10)", (int)format.format);
            return ESP_ERR_INVALID_STATE;
        }

        if (format.width != OV02C10_PPA_SRC_WIDTH_PIXELS || format.height != OV02C10_PPA_SRC_HEIGHT_PIXELS) {
            LOGE("Unsupported sensor resolution %ux%u for current preview pipeline (expected %ux%u)",
                 (unsigned int)format.width,
                 (unsigned int)format.height,
                 (unsigned int)OV02C10_PPA_SRC_WIDTH_PIXELS,
                 (unsigned int)OV02C10_PPA_SRC_HEIGHT_PIXELS);
            return ESP_ERR_INVALID_STATE;
        }

        if (format.mipi_info.lane_num != OV02C10_MIPI_DATA_LANES) {
            LOGE("Unsupported lane count %u for current CSI config (expected %u)",
                 (unsigned int)format.mipi_info.lane_num,
                 (unsigned int)OV02C10_MIPI_DATA_LANES);
            return ESP_ERR_INVALID_STATE;
        }
    }

    return ESP_OK;
}

static esp_err_t ov02c10_read_reg8(uint16_t reg, uint8_t *value)
{
    if (s_cam_sensor == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_cam_sensor_reg_val_t reg_val = {
        .regaddr = reg,
        .value = 0,
    };
    esp_err_t ret = esp_cam_sensor_ioctl(s_cam_sensor, ESP_CAM_SENSOR_IOC_G_REG, &reg_val);
    if (ret == ESP_OK) {
        *value = (uint8_t)(reg_val.value & 0xFFU);
    }
    return ret;
}

static esp_err_t ov02c10_probe_chip_id(uint32_t *out_chip_id)
{
    if (s_cam_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_cam_sensor_id_t sensor_id = {0};
    esp_err_t ret = esp_cam_sensor_ioctl(s_cam_sensor, ESP_CAM_SENSOR_IOC_G_CHIP_ID, &sensor_id);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to read OV02C10 chip ID via esp_cam_sensor");

    uint32_t chip_id = ((uint32_t)sensor_id.pid << 8) | (uint32_t)sensor_id.ver;

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

    LOGI("[OV02C10] Sensor detected. ID: 0x%06" PRIX32 " (pid=0x%04X, ver=0x%02X).",
         chip_id,
         (unsigned int)sensor_id.pid,
         (unsigned int)sensor_id.ver);
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
        .bayer_order = COLOR_RAW_ELEMENT_ORDER_GBRG,
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

#if CONFIG_BSP_ESP32P4_SILICON_REV_LOWER_3
    LOGI("ISP Pipeline: Silicon < v3.0 detected, BLC/WBG hardware bypass enabled.");
#else
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
    if (ret != ESP_OK) {
        LOGE("Failed to configure ISP BLC: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }
    ret = esp_isp_blc_enable(s_isp_proc);
    if (ret != ESP_OK) {
        LOGE("Failed to enable ISP BLC: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }
    s_isp_blc_enabled = true;

    esp_isp_wbg_config_t wbg_cfg = {
        .flags = {
            .update_once_configured = 1,
        },
    };
    ret = esp_isp_wbg_configure(s_isp_proc, &wbg_cfg);
    if (ret != ESP_OK) {
        LOGE("Failed to configure ISP WBG: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }
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
#endif

#if OV02C10_ISP_AUTO_AE_ENABLE
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

#if OV02C10_ISP_AUTO_AWB_ENABLE
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
    s_isp_rgb888_frame_size = (s_isp_rgb888_frame_size + (OV02C10_CACHE_LINE_SIZE - 1U)) & ~(OV02C10_CACHE_LINE_SIZE - 1U);
    s_isp_rgb888_frame = heap_caps_aligned_alloc(OV02C10_CACHE_LINE_SIZE,
                                                 s_isp_rgb888_frame_size,
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_isp_rgb888_frame == NULL) {
        LOGE("Failed to allocate RGB888 frame buffer in PSRAM (%u bytes)", (unsigned int)s_isp_rgb888_frame_size);
        ov02c10_release_isp_pipeline();
        return ESP_ERR_NO_MEM;
    }
    LOGI("ISP Buffer aligned @ 128B allocated at: %p", s_isp_rgb888_frame);
    LOGI("ISP RGB888 output buffer allocated in PSRAM (%u bytes)", (unsigned int)s_isp_rgb888_frame_size);

#if CONFIG_ESP_PPA_ENABLE
    ret = ov02c10_init_ppa_scaler();
    if (ret != ESP_OK) {
        ov02c10_release_isp_pipeline();
        return ret;
    }
#endif

    ret = ov02c10_start_isp_control_task();
    if (ret != ESP_OK) {
        LOGE("Failed to start ISP control task: %s", esp_err_to_name(ret));
        ov02c10_release_isp_pipeline();
        return ret;
    }

        LOGI("ISP pipeline ready (RAW10 input, RGB888 output, AE=%d, AWB=%d)",
            (int)OV02C10_ISP_AUTO_AE_ENABLE,
            (int)OV02C10_ISP_AUTO_AWB_ENABLE);
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

    ret = ov02c10_detect_sensor();
    if (ret != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = ov02c10_probe_chip_id(&chip_id);
    if (ret != ESP_OK) {
        if (s_cam_sensor) {
            esp_cam_sensor_del_dev(s_cam_sensor);
            s_cam_sensor = NULL;
        }
        return ESP_ERR_INVALID_STATE;
    }

    ret = ov02c10_init_csi_link();
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
    esp_err_t ret;

    if (s_streaming) {
        return ESP_OK;
    }

    if (!s_initialized) {
        ret = bsp_camera_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (s_cam_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(ov02c10_apply_stream_profile(), TAG, "Failed to apply OV02C10 stream profile");

#if !OV02C10_ISP_AUTO_AE_ENABLE
    (void)ov02c10_apply_manual_brightness_boost();
#endif

#if CONFIG_BSP_CAMERA_DEBUG_SENSOR_TEST_PATTERN
    ESP_RETURN_ON_ERROR(ov02c10_set_sensor_test_pattern_mode(true), TAG,
                        "Failed to enable OV02C10 test pattern mode");
    LOGW("[OV02C10] Sensor test pattern ENABLED (debug mode)");
#else
    (void)ov02c10_set_sensor_test_pattern_mode(false);
#endif

    LOGI("[OV02C10] MIPI Link ready: 2-lane @ 800Mbps. Starting stream...");
    ESP_RETURN_ON_ERROR(ov02c10_set_sensor_stream_mode(true), TAG,
                        "Failed to set OV02C10 stream mode");

    if (s_csi_ctlr) {
        if (s_isp_rgb888_frame && s_isp_rgb888_frame_size > 0) {
            ESP_RETURN_ON_ERROR(esp_cache_msync(s_isp_rgb888_frame,
                                                s_isp_rgb888_frame_size,
                                                ESP_CACHE_MSYNC_FLAG_DIR_M2C),
                                TAG,
                                "Failed to sync ISP buffer cache before CSI start");
        }
        ESP_RETURN_ON_ERROR(esp_cam_ctlr_start(s_csi_ctlr), TAG, "Failed to start CSI controller");
    }

    s_streaming = true;
    return ESP_OK;
}

esp_err_t bsp_camera_stop_stream(void)
{
    if (!s_initialized || s_cam_sensor == NULL) {
        return ESP_OK;
    }

    if (s_streaming && s_csi_ctlr) {
        (void)esp_cam_ctlr_stop(s_csi_ctlr);
    }

    ESP_RETURN_ON_ERROR(ov02c10_set_sensor_stream_mode(false), TAG,
                        "Failed to set OV02C10 standby mode");

    s_streaming = false;
    return ESP_OK;
}

esp_err_t bsp_camera_start_preview(bsp_camera_preview_frame_cb_t frame_cb, void *user_data)
{
#if CONFIG_ESP_ISP_ENABLE && CONFIG_ESP_PPA_ENABLE
    esp_err_t ret = bsp_camera_start_stream();
    if (ret != ESP_OK) {
        return ret;
    }

    if (!s_isp_rgb888_frame || !s_preview_frames[0] || !s_preview_frames[1] || !s_ppa_srm_client) {
        (void)bsp_camera_stop_stream();
        return ESP_ERR_INVALID_STATE;
    }

        LOGI("PPA Input: 1920x1080, Pitch: %d, Format: RGB888", (int)OV02C10_PPA_SRC_STRIDE_BYTES);
        LOGI("PPA Crop: %ux%u @ offset (%u,%u)",
            (unsigned int)OV02C10_PREVIEW_WIDTH,
            (unsigned int)OV02C10_PREVIEW_HEIGHT,
            (unsigned int)OV02C10_PPA_CROP_OFFSET_X,
            (unsigned int)OV02C10_PPA_CROP_OFFSET_Y);
    LOGI("PPA Output: 1024x600, Pitch: %d, Format: RGB565", (int)OV02C10_PPA_DST_STRIDE_BYTES);
    LOGI("Canvas Buffer(front): %p, Size: %d", s_preview_frames[s_preview_front_idx], (int)s_preview_frame_size);

    s_preview_frame_cb = frame_cb;
    s_preview_frame_cb_user_data = user_data;
    s_preview_enabled = true;

    ret = ov02c10_start_preview_task();
    if (ret != ESP_OK) {
        s_preview_enabled = false;
        s_preview_frame_cb = NULL;
        s_preview_frame_cb_user_data = NULL;
        (void)bsp_camera_stop_stream();
        return ret;
    }

    return ESP_OK;
#else
    (void)frame_cb;
    (void)user_data;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void bsp_camera_stop_preview(void)
{
#if CONFIG_ESP_ISP_ENABLE && CONFIG_ESP_PPA_ENABLE
    s_preview_enabled = false;
    s_preview_frame_cb = NULL;
    s_preview_frame_cb_user_data = NULL;
    ov02c10_stop_preview_task();
    (void)bsp_camera_stop_stream();
#endif
}

const uint8_t *bsp_camera_get_preview_buffer(void)
{
#if CONFIG_ESP_ISP_ENABLE && CONFIG_ESP_PPA_ENABLE
    const uint8_t *buf;
    uint8_t idx;
    portENTER_CRITICAL(&s_isp_lock);
    idx = s_preview_front_idx;
    buf = (const uint8_t *)s_preview_frames[idx];
    portEXIT_CRITICAL(&s_isp_lock);
    return buf;
#else
    return NULL;
#endif
}

size_t bsp_camera_get_preview_buffer_size(void)
{
#if CONFIG_ESP_ISP_ENABLE && CONFIG_ESP_PPA_ENABLE
    return s_preview_frame_size;
#else
    return 0;
#endif
}

uint16_t bsp_camera_get_preview_width(void)
{
    return (uint16_t)OV02C10_PREVIEW_WIDTH;
}

uint16_t bsp_camera_get_preview_height(void)
{
    return (uint16_t)OV02C10_PREVIEW_HEIGHT;
}

esp_err_t bsp_camera_get_gain_index_range(uint32_t *min_idx, uint32_t *max_idx, uint32_t *default_idx)
{
    if (min_idx == NULL || max_idx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(bsp_camera_init(), TAG, "Camera init failed while querying gain range");
    }

    if (s_cam_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_cam_sensor_param_desc_t desc = {
        .id = ESP_CAM_SENSOR_GAIN,
    };
    esp_err_t ret = esp_cam_sensor_query_para_desc(s_cam_sensor, &desc);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to query gain descriptor");

    if (desc.type != ESP_CAM_SENSOR_PARAM_TYPE_ENUMERATION || desc.enumeration.count == 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    *min_idx = 0;
    *max_idx = desc.enumeration.count - 1U;
    if (default_idx) {
        uint32_t def = (uint32_t)desc.default_value;
        if (def > *max_idx) {
            def = *max_idx;
        }
        *default_idx = def;
    }

    return ESP_OK;
}

esp_err_t bsp_camera_get_gain_index(uint32_t *gain_idx)
{
    if (gain_idx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(bsp_camera_init(), TAG, "Camera init failed while reading gain");
    }

    if (s_cam_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_cam_sensor_get_para_value(s_cam_sensor, ESP_CAM_SENSOR_GAIN, gain_idx, sizeof(*gain_idx));
}

esp_err_t bsp_camera_set_gain_index(uint32_t gain_idx)
{
    uint32_t min_idx = 0;
    uint32_t max_idx = 0;
    esp_err_t ret = bsp_camera_get_gain_index_range(&min_idx, &max_idx, NULL);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to get gain range");

    if (gain_idx < min_idx) {
        gain_idx = min_idx;
    }
    if (gain_idx > max_idx) {
        gain_idx = max_idx;
    }

    return esp_cam_sensor_set_para_value(s_cam_sensor, ESP_CAM_SENSOR_GAIN, &gain_idx, sizeof(gain_idx));
}

esp_err_t bsp_camera_get_exposure_range(uint32_t *min_exposure, uint32_t *max_exposure, uint32_t *default_exposure)
{
    if (min_exposure == NULL || max_exposure == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(bsp_camera_init(), TAG, "Camera init failed while querying exposure range");
    }

    if (s_cam_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_cam_sensor_param_desc_t desc = {
        .id = ESP_CAM_SENSOR_EXPOSURE_VAL,
    };
    esp_err_t ret = esp_cam_sensor_query_para_desc(s_cam_sensor, &desc);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to query exposure descriptor");

    if (desc.type != ESP_CAM_SENSOR_PARAM_TYPE_NUMBER) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    *min_exposure = (uint32_t)desc.number.minimum;
    *max_exposure = (uint32_t)desc.number.maximum;
    if (default_exposure) {
        uint32_t def = (uint32_t)desc.default_value;
        if (def < *min_exposure) {
            def = *min_exposure;
        }
        if (def > *max_exposure) {
            def = *max_exposure;
        }
        *default_exposure = def;
    }

    return ESP_OK;
}

esp_err_t bsp_camera_get_exposure_value(uint32_t *exposure)
{
    if (exposure == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(bsp_camera_init(), TAG, "Camera init failed while reading exposure");
    }

    if (s_cam_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_cam_sensor_get_para_value(s_cam_sensor, ESP_CAM_SENSOR_EXPOSURE_VAL, exposure, sizeof(*exposure));
}

esp_err_t bsp_camera_set_exposure_value(uint32_t exposure)
{
    uint32_t min_exposure = 0;
    uint32_t max_exposure = 0;
    esp_err_t ret = bsp_camera_get_exposure_range(&min_exposure, &max_exposure, NULL);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to get exposure range");

    if (exposure < min_exposure) {
        exposure = min_exposure;
    }
    if (exposure > max_exposure) {
        exposure = max_exposure;
    }

    return esp_cam_sensor_set_para_value(s_cam_sensor, ESP_CAM_SENSOR_EXPOSURE_VAL, &exposure, sizeof(exposure));
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

    if (s_cam_sensor) {
        esp_cam_sensor_del_dev(s_cam_sensor);
        s_cam_sensor = NULL;
    }

    if (s_cam_sccb_handle) {
        esp_sccb_del_i2c_io(s_cam_sccb_handle);
        s_cam_sccb_handle = NULL;
    }

    if (s_mclk_running) {
        ledc_stop(OV02C10_MCLK_LEDC_SPEED_MODE, OV02C10_MCLK_LEDC_CHANNEL, 0);
    }

    s_streaming = false;
    s_initialized = false;
    s_powered = false;
    s_mclk_running = false;
}
