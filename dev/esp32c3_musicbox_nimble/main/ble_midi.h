#pragma once

#include <stddef.h>
#include <stdint.h>

/** 13-bit millisecond timestamp (wraps). */
uint16_t ble_midi_timestamp_ms(void);

/** Bytes needed for one BLE MIDI packet: 2-byte header + midi_len. */
size_t ble_midi_packet_size(size_t midi_len);

/**
 * Pack one MIDI 1.0 message into a BLE MIDI characteristic value (header + timestamp + payload).
 * @param ts_ms 13-bit timestamp (only lower 13 bits used)
 * @return encoded length, or 0 if out_cap too small
 */
size_t ble_midi_encode(uint8_t *out, size_t out_cap, const uint8_t *midi, size_t midi_len,
                         uint16_t ts_ms);

/** Length of a channel voice / mode message starting with status byte @a status (0x80–0xEF). */
size_t ble_midi_message_len(uint8_t status);

/**
 * Parse one BLE MIDI characteristic write payload; invokes @a cb for each raw MIDI message.
 * Handles multi-message packets (header+ts on first sub-message, ts-only before later ones).
 */
void ble_midi_decode(const uint8_t *data, size_t len,
                     void (*cb)(const uint8_t *msg, size_t msg_len, void *user), void *user);
