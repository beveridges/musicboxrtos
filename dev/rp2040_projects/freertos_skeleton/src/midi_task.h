#ifndef MIDI_TASK_H
#define MIDI_TASK_H

#include "config.h"
#include <stddef.h>
#include <stdint.h>

TaskHandle_t midi_task_create(shared_state_t *shared);

/** Decoded BLE MIDI from ESP32 (UART framing stripped by sb1_link_task). */
void sb1_ble_midi_in(shared_state_t *sh, const uint8_t *data, size_t len);

#endif
