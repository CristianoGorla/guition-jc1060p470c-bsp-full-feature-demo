#include "bsp_radar.h"

#include <string.h>

#include "bsp_log_panel.h"
#include "driver/gpio.h"
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

#define RADAR_SAMPLE_PERIOD_MS BSP_RADAR_PROFILE_SAMPLE_PERIOD_MS
#define RADAR_TASK_STACK_SIZE 4096
#define RADAR_TASK_PRIORITY 5
#define RADAR_NEAR_MAX_MOVING_GATE BSP_RADAR_PROFILE_MOVING_GATE_MAX

static esp_err_t bsp_radar_init_out_pin(void)
{
    const gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << CONFIG_BSP_RADAR_OUT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_cfg);
    if (ret != ESP_OK) {
        LOGW("[WARN] Failed to configure RADAR_OUT GPIO%d: %s", CONFIG_BSP_RADAR_OUT_PIN, esp_err_to_name(ret));
        return ret;
    }

    LOGI("[OK] RADAR_OUT GPIO%d configured as input", CONFIG_BSP_RADAR_OUT_PIN);
    return ESP_OK;
}

static esp_err_t bsp_radar_apply_profile(LD2410_device_t *device)
{
    bool ok = true;

    /* 20cm gate resolution as requested. */
    ok = ok && ld2410_set_resolution(device, true);

    /* Keep trigger only in the near area configured by max gate. */
    ok = ok && ld2410_set_max_moving_gate(device, RADAR_NEAR_MAX_MOVING_GATE);

    /* Allow stationary targets too, but only in the same near area. */
    ok = ok && ld2410_set_max_stationary_gate(device, BSP_RADAR_PROFILE_STATIONARY_GATE_MAX);

    /* Make OUT drop quickly after no near target is present. */
    ok = ok && ld2410_set_no_one_window(device, BSP_RADAR_PROFILE_NO_ONE_WINDOW_S);

    if (!ok) {
        return ESP_FAIL;
    }

     LOGI("[OK] LD2410 profile: resolution=%ucm, moving gates=0-%d, stationary gates=0-%u (<=%ucm), no-one window=%us",
         (unsigned)BSP_RADAR_PROFILE_RESOLUTION_CM,
         RADAR_NEAR_MAX_MOVING_GATE,
         (unsigned)BSP_RADAR_PROFILE_STATIONARY_GATE_MAX,
            (unsigned)((BSP_RADAR_PROFILE_MOVING_GATE_MAX + 1U) * BSP_RADAR_PROFILE_RESOLUTION_CM),
         (unsigned)BSP_RADAR_PROFILE_NO_ONE_WINDOW_S);
    return ESP_OK;
}

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

static void bsp_radar_poll_interrupt_state(void)
{
    bsp_radar_data_t next = {0};
    bool level = (gpio_get_level((gpio_num_t)CONFIG_BSP_RADAR_OUT_PIN) > 0);
    int64_t now_ms = esp_timer_get_time() / 1000;

    taskENTER_CRITICAL(&s_radar.lock);
    next = s_radar.data;

    if (next.interrupt_timestamp_ms == 0) {
        next.interrupt_timestamp_ms = now_ms;
    }

    if (next.out_pin_high != level) {
        next.out_pin_high = level;
        next.interrupt_count += 1;
        next.interrupt_timestamp_ms = now_ms;
    }

    s_radar.data = next;
    taskEXIT_CRITICAL(&s_radar.lock);
}

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
    next.out_pin_high = (gpio_get_level((gpio_num_t)CONFIG_BSP_RADAR_OUT_PIN) > 0);
    next.distance_cm = (uint16_t)ld2410_detected_distance(device);
    next.moving_distance_cm = (uint16_t)ld2410_moving_target_distance(device);
    next.stationary_distance_cm = (uint16_t)ld2410_stationary_target_distance(device);
    next.moving_signal_pct = ld2410_moving_target_signal(device);
    next.stationary_signal_pct = ld2410_stationary_target_signal(device);
    next.energy_pct = (next.moving_signal_pct > next.stationary_signal_pct)
                        ? next.moving_signal_pct
                        : next.stationary_signal_pct;

    if (next.distance_cm == 0) {
        next.distance_cm = (next.moving_distance_cm > next.stationary_distance_cm)
                            ? next.moving_distance_cm
                            : next.stationary_distance_cm;
    }
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
        bsp_radar_poll_interrupt_state();

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
    const gpio_num_t wake_pin = (gpio_num_t)CONFIG_BSP_RADAR_OUT_PIN;
    esp_err_t ret = esp_sleep_enable_ext0_wakeup(wake_pin, 1);
    if (ret != ESP_OK) {
        LOGW("[WARN] Failed to configure LP_GPIO%d wakeup: %s", CONFIG_BSP_RADAR_OUT_PIN, esp_err_to_name(ret));
        return ret;
    }

    LOGI("[OK] LP_GPIO%d configured for wakeup", CONFIG_BSP_RADAR_OUT_PIN);
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

    {
        esp_err_t profile_ret = bsp_radar_apply_profile(s_radar.device);
        if (profile_ret != ESP_OK) {
            LOGE("[ERR] Failed to apply LD2410 profile");
            ld2410_free(s_radar.device);
            s_radar.device = NULL;
            return profile_ret;
        }
    }

    (void)bsp_radar_init_out_pin();

    taskENTER_CRITICAL(&s_radar.lock);
    s_radar.data.initialized = true;
    s_radar.data.out_pin_high = (gpio_get_level((gpio_num_t)CONFIG_BSP_RADAR_OUT_PIN) > 0);
    s_radar.data.interrupt_timestamp_ms = esp_timer_get_time() / 1000;
    taskEXIT_CRITICAL(&s_radar.lock);

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
