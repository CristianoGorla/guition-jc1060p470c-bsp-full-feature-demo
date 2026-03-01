#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_hosted_wifi.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "wifi_hosted";
static EventGroupHandle_t wifi_event_group;
const int IP_GOT_BIT = BIT0;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(wifi_event_group, IP_GOT_BIT);
    }
}

void init_wifi(void)
{
    ESP_LOGI(TAG, "Inizializzazione interfaccia Wi-Fi Hosted..."); // FIX: Ora TAG è usato
    wifi_event_group = xEventGroupCreate();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
}

bool check_if_already_has_ip(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    return (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0);
}

bool do_wifi_scan_and_check(const char *target)
{
    wifi_scan_config_t sc = {.ssid = (uint8_t *)target};
    esp_wifi_scan_start(&sc, true);
    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    return count > 0;
}

void wifi_connect(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, 32);
    strncpy((char *)cfg.sta.password, pass, 64);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();
}

void wait_for_ip(void)
{
    xEventGroupWaitBits(wifi_event_group, IP_GOT_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
}