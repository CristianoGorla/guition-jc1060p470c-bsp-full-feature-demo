#include "bsp_radar.h"

#include <string.h>

#include "bsp_log_panel.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ld2410.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#define LOG_UNIT "RADAR"
#define LOGI(fmt, ...) BSP_LOGI_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) BSP_LOGW_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) BSP_LOGE_PANEL(LOG_UNIT, fmt, ##__VA_ARGS__)

#define RADAR_SAMPLE_PERIOD_MS 50
#define RADAR_TASK_STACK_SIZE 4096
#define RADAR_TASK_PRIORITY 5

typedef struct {
    LD2410_device_t *device;
    TaskHandle_t sample_task;
    bsp_radar_data_t data;
    portMUX_TYPE lock;
} bsp_radar_context_t;

static bsp_radar_context_t s_radar = {
    .device = NULL,
    .sample_task = NULL,
    .data = {0},
    .lock = portMUX_INITIALIZER_UNLOCKED,
};

static void bsp_radar_update_data(LD2410_device_t *device)
{
    bsp_radar_data_t next = {0};

    taskENTER_CRITICAL(&s_radar.lock);
    next = s_radar.data;
    taskEXIT_CRITICAL(&s_radar.lock);

    next.initialized = true;
    next.status = ld2410_get_status(device);
    next.present = ld2410_presence_detected(device);
    next.moving_target = ld2410_moving_target_detected(device);
    next.stationary_target = ld2410_stationary_target_detected(device);
    next.distance_cm = (uint16_t)ld2410_detected_distance(device);
    next.moving_distance_cm = (uint16_t)ld2410_moving_target_distance(device);
    next.stationary_distance_cm = (uint16_t)ld2410_stationary_target_distance(device);
    next.sample_count += 1;
    next.timestamp_ms = esp_timer_get_time() / 1000;

    taskENTER_CRITICAL(&s_radar.lock);
    s_radar.data = next;
    taskEXIT_CRITICAL(&s_radar.lock);

#if !CONFIG_BSP_RADAR_LOG_SILENT
    LOGI("Presence=%s Dist=%ucm Status=%u",
         next.present ? "ON" : "OFF",
         (unsigned)next.distance_cm,
         (unsigned)next.status);
#endif
}

static void bsp_radar_sample_task(void *arg)
{
    (void)arg;

    while (1) {
        Response_t resp = ld2410_check(s_radar.device);
        if (resp == RP_DATA) {
            bsp_radar_update_data(s_radar.device);
        }
        vTaskDelay(pdMS_TO_TICKS(RADAR_SAMPLE_PERIOD_MS));
    }
}

static esp_err_t bsp_radar_configure_wakeup(void)
{
#if CONFIG_BSP_RADAR_SLEEP_WAKEUP
#if SOC_PM_SUPPORT_EXT0_WAKEUP
    esp_err_t ret = esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 1);
    if (ret != ESP_OK) {
        LOGW("[WARN] Failed to configure LP_GPIO4 wakeup: %s", esp_err_to_name(ret));
        return ret;
    }

    LOGI("[OK] LP_GPIO4 configured for wakeup");
#else
    LOGW("[WARN] ext0 wakeup is not supported on this target");
    return ESP_ERR_NOT_SUPPORTED;
#endif
#else
    LOGI("[OK] Deep sleep wakeup disabled by config");
#endif
    return ESP_OK;
}

esp_err_t bsp_radar_init(void)
{
    if (s_radar.device != NULL) {
        return ESP_OK;
    }

    memset(&s_radar.data, 0, sizeof(s_radar.data));

    s_radar.device = ld2410_new();
    if (s_radar.device == NULL) {
        LOGE("[ERR] Failed to allocate LD2410 device");
        return ESP_ERR_NO_MEM;
    }

    if (!ld2410_begin(s_radar.device)) {
        LOGE("[ERR] Failed to communicate with HLK-LD2410C");
        ld2410_free(s_radar.device);
        s_radar.device = NULL;
        return ESP_FAIL;
    }

    BaseType_t task_ok = xTaskCreate(
        bsp_radar_sample_task,
        "bsp_radar_sample",
        RADAR_TASK_STACK_SIZE,
        NULL,
        RADAR_TASK_PRIORITY,
        &s_radar.sample_task);
    if (task_ok != pdPASS) {
        LOGE("[ERR] Failed to start radar sampling task");
        ld2410_free(s_radar.device);
        s_radar.device = NULL;
        return ESP_ERR_NO_MEM;
    }

    LOGI("[OK] HLK-LD2410C initialized on UART%d", CONFIG_BSP_RADAR_UART_PORT);
    (void)bsp_radar_configure_wakeup();

    return ESP_OK;
}

esp_err_t bsp_radar_get_data(bsp_radar_data_t *out_data)
{
    if (out_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL(&s_radar.lock);
    *out_data = s_radar.data;
    taskEXIT_CRITICAL(&s_radar.lock);

    return out_data->initialized ? ESP_OK : ESP_ERR_INVALID_STATE;
}

const bsp_radar_data_t *bsp_radar_get_data_ptr(void)
{
    return &s_radar.data;
}

void bsp_radar_deinit(void)
{
    if (s_radar.sample_task != NULL) {
        vTaskDelete(s_radar.sample_task);
        s_radar.sample_task = NULL;
    }

    if (s_radar.device != NULL) {
        ld2410_free(s_radar.device);
        s_radar.device = NULL;
    }

    memset(&s_radar.data, 0, sizeof(s_radar.data));
}
