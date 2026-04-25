#include "wifi_sb1.h"

#include <string.h>

#include "config_store.h"
#include "sb1_uart_rp2040.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"

static const char *TAG = "sb1_wifi";

static bool s_sta_got_ip;

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_got_ip = false;
        sb1_uart_send_wifi_status(false);
        ESP_LOGW(TAG, "STA disconnected, retrying");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_sta_got_ip = true;
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "STA got ip: " IPSTR, IP2STR(&e->ip_info.ip));
        sb1_uart_send_wifi_status(true);
    }
}

bool sb1_wifi_sta_got_ip(void)
{
    return s_sta_got_ip;
}

void sb1_wifi_init(void)
{
    s_sta_got_ip = false;

    sb1_settings_t cfg;
    sb1_settings_defaults(&cfg);
    ESP_ERROR_CHECK(sb1_settings_load(&cfg));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi, NULL));

    wifi_config_t ap = {0};
    const char *ap_ssid = "SB1-Config";
    strncpy((char *)ap.ap.ssid, ap_ssid, sizeof(ap.ap.ssid) - 1);
    ap.ap.ssid_len = (uint8_t)strlen(ap_ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;

    bool want_sta = cfg.sta_ssid[0] != '\0';

    if (want_sta) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

        wifi_config_t sta = {0};
        strncpy((char *)sta.sta.ssid, cfg.sta_ssid, sizeof(sta.sta.ssid) - 1);
        strncpy((char *)sta.sta.password, cfg.sta_pass, sizeof(sta.sta.password) - 1);
        sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi: SoftAP '%s' (open). STA %s.", ap_ssid, want_sta ? "enabled" : "disabled");
    sb1_uart_send_wifi_status(sb1_wifi_sta_got_ip());
}
