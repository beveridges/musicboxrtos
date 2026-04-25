#pragma once

#include <stdbool.h>

#include "esp_err.h"

void sb1_wifi_init(void);
bool sb1_wifi_sta_got_ip(void);
