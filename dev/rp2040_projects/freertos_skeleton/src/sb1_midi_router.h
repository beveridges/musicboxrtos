#pragma once

#include "config.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
  SB1_MIDI_SRC_LOCAL = 0,
  SB1_MIDI_SRC_BLE = 1,
} sb1_midi_src_t;

/** Route one raw MIDI message according to shared policy. */
void sb1_midi_router_route(shared_state_t *sh, sb1_midi_src_t src, const uint8_t *data, size_t len);

/** Handle BLE MIDI ingress from ESP32 framed UART path. */
void sb1_midi_router_ble_ingress(shared_state_t *sh, const uint8_t *data, size_t len);
