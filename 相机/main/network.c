#include "app.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_log.h"

static const char *TAG = "network";
static esp_netif_t *ap_netif;
static char ap_name[32];

void network_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    // Do not read, overwrite, or erase previously saved router credentials.
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(ap_name, sizeof(ap_name), "Camera-%02X%02X", mac[4], mac[5]);
    wifi_config_t ap = {.ap = {.channel = 1, .authmode = WIFI_AUTH_OPEN,
        .max_connection = 2, .pmf_cfg = {.required = false}}};
    strlcpy((char *)ap.ap.ssid, ap_name, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(ap_name);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Camera hotspot: %s (open, no password); watch at http://192.168.4.1/", ap_name);
}

const char *network_ap_name(void) { return ap_name; }
void network_status(char *ip, size_t len, int *clients)
{
    esp_netif_ip_info_t info = {0};
    esp_netif_get_ip_info(ap_netif, &info);
    snprintf(ip, len, IPSTR, IP2STR(&info.ip));
    wifi_sta_list_t list = {0};
    *clients = esp_wifi_ap_get_sta_list(&list) == ESP_OK ? list.num : 0;
}
