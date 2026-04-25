#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define SB1_STA_SSID_MAX 32
#define SB1_STA_PASS_MAX 64
#define SB1_DEVICE_NAME_MAX 31

typedef struct {
    char sta_ssid[SB1_STA_SSID_MAX + 1];
    char sta_pass[SB1_STA_PASS_MAX + 1];
    char device_name[SB1_DEVICE_NAME_MAX + 1];
    uint32_t uart_baud;
} sb1_settings_t;

void sb1_settings_defaults(sb1_settings_t *out);
esp_err_t sb1_settings_load(sb1_settings_t *out);
esp_err_t sb1_settings_save(const sb1_settings_t *in);
