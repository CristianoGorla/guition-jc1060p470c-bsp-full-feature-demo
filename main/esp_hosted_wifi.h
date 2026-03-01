#ifndef __ESP_HOSTED_WIFI_H__
#define __ESP_HOSTED_WIFI_H__
#include <stdbool.h>
void init_wifi(void);
bool do_wifi_scan_and_check(const char *target_ssid);
void wifi_connect(const char *ssid, const char *password);
void wait_for_ip(void);
bool check_if_already_has_ip(void);
#endif