/**
 * @file bsp_heartbeat.c
 * @brief System heartbeat monitoring implementation
 */

#include "bsp_heartbeat.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_HEARTBEAT";

#ifdef CONFIG_BSP_ENABLE_HEARTBEAT

static TaskHandle_t s_heartbeat_task = NULL;
static bool s_task_running = false;

/**
 * @brief Heartbeat monitoring task
 */
static void heartbeat_task(void *arg)
{
    uint32_t beat_count = 0;

    ESP_LOGI(TAG, "Started (interval: %d ms, priority: %d)",
             CONFIG_BSP_HEARTBEAT_INTERVAL_MS,
             CONFIG_BSP_HEARTBEAT_TASK_PRIORITY);

    while (s_task_running) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_BSP_HEARTBEAT_INTERVAL_MS));
        beat_count++;

        /* Collect system stats */
        uint32_t uptime_sec = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

        /* Log heartbeat */
        ESP_LOGI(TAG, "[%lu] Uptime: %lu s | PSRAM: %zu KB | Internal: %zu KB",
                 (unsigned long)beat_count,
                 (unsigned long)uptime_sec,
                 free_psram / 1024,
                 free_internal / 1024);
    }

    ESP_LOGI(TAG, "Stopped");
    s_heartbeat_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t bsp_heartbeat_start(void)
{
    if (s_task_running) {
        ESP_LOGW(TAG, "Already running");
        return ESP_ERR_INVALID_STATE;
    }

    s_task_running = true;

    BaseType_t ret = xTaskCreate(
        heartbeat_task,
        "bsp_heartbeat",
        CONFIG_BSP_HEARTBEAT_TASK_STACK_SIZE,
        NULL,
        CONFIG_BSP_HEARTBEAT_TASK_PRIORITY,
        &s_heartbeat_task
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        s_task_running = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t bsp_heartbeat_stop(void)
{
    if (!s_task_running) {
        ESP_LOGW(TAG, "Not running");
        return ESP_ERR_INVALID_STATE;
    }

    s_task_running = false;

    /* Wait for task cleanup */
    while (s_heartbeat_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return ESP_OK;
}

bool bsp_heartbeat_is_running(void)
{
    return s_task_running;
}

#else /* CONFIG_BSP_ENABLE_HEARTBEAT */

esp_err_t bsp_heartbeat_start(void)
{
    ESP_LOGD(TAG, "Disabled at compile time (CONFIG_BSP_ENABLE_HEARTBEAT=n)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_heartbeat_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool bsp_heartbeat_is_running(void)
{
    return false;
}

#endif /* CONFIG_BSP_ENABLE_HEARTBEAT */
