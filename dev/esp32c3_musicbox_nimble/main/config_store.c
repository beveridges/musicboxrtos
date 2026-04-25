#include "config_store.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

#define NVS_NS "sb1_cfg"
#define K_STA_SSID "sta_ssid"
#define K_STA_PASS "sta_pass"
#define K_DEV_NAME "dev_name"
#define K_UART_BAUD "uart_baud"

#define DEFAULT_NAME "SB1 MIDI INTERFACE"
#define DEFAULT_BAUD 115200

void sb1_settings_defaults(sb1_settings_t *out)
{
    memset(out, 0, sizeof(*out));
    strncpy(out->device_name, DEFAULT_NAME, sizeof(out->device_name) - 1);
    out->uart_baud = DEFAULT_BAUD;
}

esp_err_t sb1_settings_load(sb1_settings_t *out)
{
    sb1_settings_defaults(out);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t len = sizeof(out->sta_ssid);
    err = nvs_get_str(h, K_STA_SSID, out->sta_ssid, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return err;
    }

    len = sizeof(out->sta_pass);
    err = nvs_get_str(h, K_STA_PASS, out->sta_pass, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return err;
    }

    len = sizeof(out->device_name);
    err = nvs_get_str(h, K_DEV_NAME, out->device_name, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return err;
    }

    uint32_t baud = DEFAULT_BAUD;
    err = nvs_get_u32(h, K_UART_BAUD, &baud);
    if (err == ESP_OK) {
        out->uart_baud = baud;
    }

    nvs_close(h);
    return ESP_OK;
}

esp_err_t sb1_settings_save(const sb1_settings_t *in)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(h, K_STA_SSID, in->sta_ssid);
    if (err != ESP_OK) {
        goto out;
    }
    err = nvs_set_str(h, K_STA_PASS, in->sta_pass);
    if (err != ESP_OK) {
        goto out;
    }
    err = nvs_set_str(h, K_DEV_NAME, in->device_name);
    if (err != ESP_OK) {
        goto out;
    }
    err = nvs_set_u32(h, K_UART_BAUD, in->uart_baud);
    if (err != ESP_OK) {
        goto out;
    }
    err = nvs_commit(h);

out:
    nvs_close(h);
    return err;
}
