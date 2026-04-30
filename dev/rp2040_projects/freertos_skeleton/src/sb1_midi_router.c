#include "sb1_midi_router.h"
#include "sb1_uart_midi.h"
#include "pico/time.h"
#include "tusb.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>

static void sb1_ble_update_rx_telemetry(shared_state_t *sh, const uint8_t *data, size_t len) {
  if (!sh || !data || len == 0 || !sh->mutex) {
    return;
  }
  uint32_t now_ms = to_ms_since_boot(get_absolute_time());
  if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
    return;
  }
  sh->ble_rx_packets_total++;
  sh->ble_rx_last_ms = now_ms;
  if (len >= 3u && (data[0] & 0x80u)) {
    uint8_t status = data[0];
    uint8_t typ = (uint8_t)(status & 0xF0u);
    unsigned ch = (unsigned)((status & 0x0Fu) + 1u);
    if (typ == 0x90u && data[2] != 0u) {
      snprintf(sh->ble_rx_last_summary, LINE_LEN, "RX NOn ch%u %u v%u", ch, (unsigned)data[1],
               (unsigned)data[2]);
    } else if (typ == 0x80u || (typ == 0x90u && data[2] == 0u)) {
      snprintf(sh->ble_rx_last_summary, LINE_LEN, "RX NOf ch%u %u", ch, (unsigned)data[1]);
    } else if (typ == 0xB0u) {
      snprintf(sh->ble_rx_last_summary, LINE_LEN, "RX CC ch%u %u=%u", ch, (unsigned)data[1],
               (unsigned)data[2]);
    } else if (typ == 0xE0u) {
      unsigned bend = (unsigned)(((uint16_t)data[2] << 7) | (uint16_t)(data[1] & 0x7Fu));
      snprintf(sh->ble_rx_last_summary, LINE_LEN, "RX PB ch%u %u", ch, bend);
    } else {
      snprintf(sh->ble_rx_last_summary, LINE_LEN, "RX ST %02X %u", (unsigned)status, (unsigned)len);
    }
  } else {
    snprintf(sh->ble_rx_last_summary, LINE_LEN, "RX RAW %u", (unsigned)len);
    sh->ble_rx_overrun_count++;
  }
  xSemaphoreGive(sh->mutex);
}

void sb1_midi_router_route(shared_state_t *sh, sb1_midi_src_t src, const uint8_t *data, size_t len) {
  if (!sh || !data || len == 0) {
    return;
  }

  /* RP2040-local events are always eligible for BLE egress when a peer is connected. */
  if (src == SB1_MIDI_SRC_LOCAL) {
    sb1_uart_mirror_midi(data, len, sh);
  }

#if CFG_TUD_MIDI
  if (!tud_mounted()) {
    return;
  }

  if (src == SB1_MIDI_SRC_BLE) {
    uint8_t mode = SB1_BLE_MIDI_SINK_MERGE;
    if (sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      mode = sh->ble_midi_sink;
      if (mode > SB1_BLE_MIDI_SINK_DEVICE) {
        mode = SB1_BLE_MIDI_SINK_MERGE;
      }
      xSemaphoreGive(sh->mutex);
    }
    if (mode == SB1_BLE_MIDI_SINK_DEVICE) {
      return;
    }
  }

  (void)tud_midi_stream_write(0, data, len);
#else
  (void)src;
#endif
}

void sb1_midi_router_ble_ingress(shared_state_t *sh, const uint8_t *data, size_t len) {
  sb1_ble_update_rx_telemetry(sh, data, len);
  sb1_midi_router_route(sh, SB1_MIDI_SRC_BLE, data, len);
}
