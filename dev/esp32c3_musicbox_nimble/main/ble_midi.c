#include "ble_midi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint16_t ble_midi_timestamp_ms(void)
{
    return (uint16_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) & 0x1FFFu;
}

size_t ble_midi_packet_size(size_t midi_len)
{
    return 2u + midi_len;
}

size_t ble_midi_encode(uint8_t *out, size_t out_cap, const uint8_t *midi, size_t midi_len,
                       uint16_t ts_ms)
{
    if (out_cap < 2u + midi_len) {
        return 0;
    }
    ts_ms &= 0x1FFFu;
    out[0] = (uint8_t)(0x80u | ((ts_ms >> 7) & 0x3Fu));
    out[1] = (uint8_t)(0x80u | (ts_ms & 0x7Fu));
    if (midi_len > 0 && midi != NULL) {
        for (size_t i = 0; i < midi_len; i++) {
            out[2u + i] = midi[i];
        }
    }
    return 2u + midi_len;
}

size_t ble_midi_message_len(uint8_t status)
{
    if (status < 0x80u) {
        return 0;
    }
    if (status < 0xF0u) {
        uint8_t hi = status & 0xF0u;
        if (hi == 0xC0u || hi == 0xD0u) {
            return 2;
        }
        return 3;
    }
    switch (status) {
    case 0xF0u:
        return 0;
    case 0xF7u:
        return 1;
    case 0xF1u:
    case 0xF3u:
        return 2;
    case 0xF2u:
        return 3;
    default:
        return 1;
    }
}

static void emit_one(const uint8_t *p, size_t len, size_t *i, void (*cb)(const uint8_t *, size_t, void *),
                     void *user)
{
    if (*i >= len) {
        return;
    }
    uint8_t st = p[*i];
    if (st < 0x80u) {
        (*i)++;
        return;
    }
    if (st == 0xF0u) {
        size_t j = *i + 1;
        while (j < len && p[j] != 0xF7u) {
            j++;
        }
        if (j < len && p[j] == 0xF7u) {
            cb(p + *i, j - *i + 1, user);
            *i = j + 1;
        } else {
            cb(p + *i, len - *i, user);
            *i = len;
        }
        return;
    }

    size_t mlen = ble_midi_message_len(st);
    if (mlen == 0 || *i + mlen > len) {
        (*i)++;
        return;
    }
    cb(p + *i, mlen, user);
    *i += mlen;
}

void ble_midi_decode(const uint8_t *data, size_t len,
                     void (*cb)(const uint8_t *msg, size_t msg_len, void *user), void *user)
{
    if (data == NULL || len == 0 || cb == NULL) {
        return;
    }

    size_t i = 0;
    bool first = true;

    while (i < len) {
        if (first) {
            if (len - i < 2u) {
                break;
            }
            i += 2u;
            first = false;
        } else {
            if (i >= len) {
                break;
            }
            i += 1u;
        }
        if (i >= len) {
            break;
        }
        emit_one(data, len, &i, cb, user);
    }
}
