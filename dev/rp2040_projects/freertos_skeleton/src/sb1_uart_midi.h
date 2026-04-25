#pragma once

#include "config.h"
#include <stddef.h>
#include <stdint.h>

/** Mirror raw MIDI bytes to ESP32 UART when BLE peer connected (for BLE MIDI notify). */
void sb1_uart_mirror_midi(const uint8_t *data, size_t len, shared_state_t *sh);
