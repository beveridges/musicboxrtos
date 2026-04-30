/*
 * UART0 RX: SB1BT / SB1WF status lines (ESP32 -> RP2040) and framed BLE MIDI relay.
 * SB1BT,0 / SB1BT,1,<name> are sent by ESP32 musicbox_nimble (sb1_uart_send_bt_status in main.c) when MUSICBOX_UART_BRIDGE.
 */
#include "sb1_link_task.h"
#include "config.h"
#include "midi_task.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "FreeRTOS.h"
#include "task.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SB1_LINK_TASK_STACK  768u
#define SB1_LINK_PRIORITY    1
#define SB1_LINK_LINE_MAX    160
#define SB1_MIDI_FRAME_MAX   128u
/* If a framed UART packet stalls (lost/corrupt byte), resync to line mode so SB1BT/SB1WF status
 * updates (especially disconnect) are not blocked indefinitely. */
#define SB1_RX_FRAME_STALL_MS 150u
#define SB1_BT_TOAST_MS      1800u

typedef enum { RX_LINE, RX_LEN, RX_PAYLOAD } rx_mode_t;

static shared_state_t *s_sh;

static void sb1_apply_wifi_scan_line(shared_state_t *sh, const char *line) {
  if (!sh || !sh->mutex || !line || strncmp(line, "SB1WFS,", 7) != 0) {
    return;
  }
  if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return;
  }
  const char *p = line + 7;
  if (strncmp(p, "BEGIN", 5) == 0) {
    sh->connectivity_wifi_scanning = true;
    sh->connectivity_wifi_scan_count = 0u;
    sh->connectivity_wifi_scan_sel = 0u;
    sh->connectivity_wifi_status[0] = '\0';
  } else if (strncmp(p, "SSID,", 5) == 0) {
    if (sh->connectivity_wifi_scan_count < SB1_WIFI_SCAN_MAX_RESULTS) {
      uint8_t idx = sh->connectivity_wifi_scan_count;
      strncpy(sh->connectivity_wifi_ssids[idx], p + 5, SB1_WIFI_SSID_MAX);
      sh->connectivity_wifi_ssids[idx][SB1_WIFI_SSID_MAX] = '\0';
      sh->connectivity_wifi_scan_count++;
    }
  } else if (strncmp(p, "DONE,", 5) == 0) {
    sh->connectivity_wifi_scanning = false;
    uint8_t cnt = (uint8_t)atoi(p + 5);
    if (cnt > sh->connectivity_wifi_scan_count) {
      cnt = sh->connectivity_wifi_scan_count;
    }
    if (sh->connectivity_wifi_scan_count == 0u) {
      snprintf(sh->connectivity_wifi_status, sizeof(sh->connectivity_wifi_status), "NO HOTSPOTS");
    } else {
      sh->connectivity_wifi_status[0] = '\0';
    }
    if (sh->connectivity_sel >= cnt && cnt > 0u) {
      sh->connectivity_sel = (uint8_t)(cnt - 1u);
    }
  } else if (strncmp(p, "INFO,", 5) == 0) {
    snprintf(sh->connectivity_wifi_status, sizeof(sh->connectivity_wifi_status), "%s", p + 5);
  } else if (strncmp(p, "ERR,", 4) == 0) {
    sh->connectivity_wifi_scanning = false;
    snprintf(sh->connectivity_wifi_status, sizeof(sh->connectivity_wifi_status), "%s", p + 4);
  }
  sh->bt_pairing_dirty = true;
  sh->menu_dirty = true;
  xSemaphoreGive(sh->mutex);
}

static uint8_t sb1_ble_state_from_name(const char *s) {
  if (!s) {
    return SB1_BLE_STATE_BOOTING;
  }
  if (strcmp(s, "ADVERTISING") == 0) {
    return SB1_BLE_STATE_ADVERTISING;
  }
  if (strcmp(s, "CONNECTED") == 0) {
    return SB1_BLE_STATE_CONNECTED;
  }
  if (strcmp(s, "RECOVERING") == 0) {
    return SB1_BLE_STATE_RECOVERING;
  }
  if (strcmp(s, "FAULT") == 0) {
    return SB1_BLE_STATE_FAULT;
  }
  return SB1_BLE_STATE_BOOTING;
}

static void sb1_apply_ble_telem_line(shared_state_t *sh, const char *line) {
  if (!sh || !sh->mutex || !line || strncmp(line, "SB1BLE,", 7) != 0) {
    return;
  }
  if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return;
  }
  const char *p = line + 7;
  if (strncmp(p, "STATE,", 6) == 0) {
    sh->ble_state = sb1_ble_state_from_name(p + 6);
  } else if (strncmp(p, "ADV,", 4) == 0) {
    sh->ble_adv_active = (p[4] == '1');
  } else if (strncmp(p, "ERR,", 4) == 0) {
    sh->ble_last_err = (int16_t)atoi(p + 4);
  } else if (strncmp(p, "DISC,", 5) == 0) {
    sh->ble_last_disc_reason = (int16_t)atoi(p + 5);
  } else if (strncmp(p, "RECOV,", 6) == 0) {
    sh->ble_recovery_count = (uint32_t)atoi(p + 6);
  } else if (strncmp(p, "PROTO,", 6) == 0) {
    sh->ble_proto_version = (uint8_t)atoi(p + 6);
  }
  sh->bt_pairing_dirty = true;
  sh->menu_dirty = true;
  xSemaphoreGive(sh->mutex);
}

static void sb1_sanitize_peer_name(char *dst, size_t dst_cap, const char *src) {
  size_t j = 0;
  for (size_t i = 0; src[i] != '\0' && j + 1 < dst_cap; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c >= 'a' && c <= 'z') {
      c = (unsigned char)toupper((int)c);
    }
    /* Printable ASCII — must not stop at 'Z'; names/addrs can contain _ [ ] etc. */
    if (c >= 0x20 && c <= 0x7E) {
      dst[j++] = (char)c;
    }
  }
  dst[j] = '\0';
  if (j == 0) {
    strncpy(dst, "CONNECTED", dst_cap - 1);
    dst[dst_cap - 1] = '\0';
  }
}

static void sb1_apply_bt_link_state(shared_state_t *sh, bool connected, const char *peer_name) {
  if (!sh || !sh->mutex) {
    return;
  }
  uint32_t now_ms = to_ms_since_boot(get_absolute_time());
  if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
    return;
  }
  bool changed = (sh->bt_peer_connected != connected);
  sh->bt_peer_connected = connected;
  if (connected) {
    if (peer_name && peer_name[0] != '\0') {
      strncpy(sh->bt_peer_name, peer_name, BT_PEER_NAME_MAX - 1);
      sh->bt_peer_name[BT_PEER_NAME_MAX - 1] = '\0';
    } else {
      strncpy(sh->bt_peer_name, "CONNECTED", BT_PEER_NAME_MAX - 1);
      sh->bt_peer_name[BT_PEER_NAME_MAX - 1] = '\0';
    }
    if (changed) {
      sh->bt_session_count++;
    }
    sh->bt_toast_kind = 1u;
    sh->bt_toast_until_ms = now_ms + SB1_BT_TOAST_MS;
  } else {
    /* Keep bt_peer_name for LCD (last peer) until the next SB1BT,1,... overwrites it. */
    sh->ble_rx_last_ms = 0u;
    sh->ble_rx_last_summary[0] = '\0';
    sh->bt_toast_kind = 2u;
    sh->bt_toast_until_ms = now_ms + SB1_BT_TOAST_MS;
  }
  if (changed) {
    sh->bt_last_change_ms = now_ms;
  }
  sh->connectivity_connecting_bt = false;
  sh->bt_pairing_dirty = true;
  sh->menu_dirty = true;
  xSemaphoreGive(sh->mutex);
}

static void sb1_link_task_fn(void *pvParameters) {
  (void)pvParameters;
  char line[SB1_LINK_LINE_MAX];
  size_t len = 0;
  rx_mode_t mode = RX_LINE;
  uint8_t pay_len = 0;
  uint8_t pay_got = 0;
  uint8_t pay_buf[SB1_MIDI_FRAME_MAX];
  uint32_t mode_since_ms = to_ms_since_boot(get_absolute_time());

  for (;;) {
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) {
      if (mode != RX_LINE) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if ((uint32_t)(now_ms - mode_since_ms) > SB1_RX_FRAME_STALL_MS) {
          mode = RX_LINE;
          len = 0;
          pay_len = 0;
          pay_got = 0;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (c < 0) {
      continue;
    }

    if (mode == RX_PAYLOAD) {
      pay_buf[pay_got++] = (uint8_t)c;
      if (pay_got >= pay_len) {
        if (s_sh) {
          sb1_ble_midi_in(s_sh, pay_buf, pay_len);
        }
        mode = RX_LINE;
        len = 0;
        pay_got = 0;
        pay_len = 0;
        mode_since_ms = to_ms_since_boot(get_absolute_time());
      }
      continue;
    }

    if (mode == RX_LEN) {
      pay_len = (uint8_t)c;
      if (pay_len == 0 || pay_len > SB1_MIDI_FRAME_MAX) {
        mode = RX_LINE;
        len = 0;
        pay_len = 0;
        pay_got = 0;
        mode_since_ms = to_ms_since_boot(get_absolute_time());
      } else {
        pay_got = 0;
        mode = RX_PAYLOAD;
        mode_since_ms = to_ms_since_boot(get_absolute_time());
      }
      continue;
    }

    /* RX_LINE */
    if ((unsigned char)c == SB1_UART_FRAME_SOH) {
      len = 0;
      mode = RX_LEN;
      mode_since_ms = to_ms_since_boot(get_absolute_time());
      continue;
    }

    if (c >= 0 && c != '\r') {
      if (c == '\n') {
        line[len] = '\0';
        len = 0;
        if (strncmp(line, "SB1BT,", 6) == 0) {
          const char *p = line + 6;
          if (*p == '0' && (p[1] == '\0' || p[1] == '\n')) {
            sb1_apply_bt_link_state(s_sh, false, NULL);
          } else if (*p == '1' && p[1] == ',') {
            const char *name_in = p + 2;
            char nameBuf[BT_PEER_NAME_MAX];
            sb1_sanitize_peer_name(nameBuf, sizeof(nameBuf), name_in);
            sb1_apply_bt_link_state(s_sh, true, nameBuf);
          }
        } else if (strncmp(line, "SB1WF,", 6) == 0) {
          const char *p = line + 6;
          if (*p == '0' && (p[1] == '\0' || p[1] == '\n')) {
            if (s_sh && s_sh->mutex && xSemaphoreTake(s_sh->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
              s_sh->wifi_sta_connected = false;
              s_sh->connectivity_connecting_wifi = false;
              snprintf(s_sh->connectivity_wifi_status, sizeof(s_sh->connectivity_wifi_status), "DISCONNECTED");
              s_sh->bt_pairing_dirty = true;
              s_sh->menu_dirty = true;
              xSemaphoreGive(s_sh->mutex);
            }
          } else if (*p == '1' && (p[1] == '\0' || p[1] == '\n')) {
            if (s_sh && s_sh->mutex && xSemaphoreTake(s_sh->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
              s_sh->wifi_sta_connected = true;
              s_sh->connectivity_connecting_wifi = false;
              snprintf(s_sh->connectivity_wifi_status, sizeof(s_sh->connectivity_wifi_status), "CONNECTED");
              s_sh->bt_pairing_dirty = true;
              s_sh->menu_dirty = true;
              xSemaphoreGive(s_sh->mutex);
            }
          }
        } else if (strncmp(line, "SB1WFS,", 7) == 0) {
          sb1_apply_wifi_scan_line(s_sh, line);
        } else if (strncmp(line, "SB1BLE,", 7) == 0) {
          sb1_apply_ble_telem_line(s_sh, line);
        }
      } else if (len + 1 < sizeof(line)) {
        line[len++] = (char)c;
      } else {
        len = 0;
      }
    }
  }
}

void sb1_link_task_create(shared_state_t *shared) {
  s_sh = shared;
  xTaskCreate(sb1_link_task_fn, "sb1_link", SB1_LINK_TASK_STACK, NULL, SB1_LINK_PRIORITY, NULL);
}
