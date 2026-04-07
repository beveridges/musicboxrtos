/*
 * Drain UART0 RX (ESP32 → RP2040) for SB1 BLE status lines: SB1BT,0 / SB1BT,1,<name>
 */
#include "sb1_link_task.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define SB1_LINK_TASK_STACK  512u
#define SB1_LINK_PRIORITY    1
#define SB1_LINK_LINE_MAX    160

static shared_state_t *s_sh;

static void sb1_sanitize_peer_name(char *dst, size_t dst_cap, const char *src) {
  size_t j = 0;
  for (size_t i = 0; src[i] != '\0' && j + 1 < dst_cap; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c >= 'a' && c <= 'z') {
      c = (unsigned char)toupper((int)c);
    }
    if (c >= 0x20 && c <= 0x5A) {
      dst[j++] = (char)c;
    }
  }
  dst[j] = '\0';
  if (j == 0) {
    strncpy(dst, "CONNECTED", dst_cap - 1);
    dst[dst_cap - 1] = '\0';
  }
}

static void sb1_link_task_fn(void *pvParameters) {
  (void)pvParameters;
  char line[SB1_LINK_LINE_MAX];
  size_t len = 0;

  for (;;) {
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (c >= 0 && c != '\r') {
      if (c == '\n') {
        line[len] = '\0';
        len = 0;
        if (strncmp(line, "SB1BT,", 6) == 0) {
          const char *p = line + 6;
          if (*p == '0' && (p[1] == '\0' || p[1] == '\n')) {
            if (s_sh && s_sh->mutex && xSemaphoreTake(s_sh->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
              s_sh->bt_peer_connected = false;
              s_sh->bt_peer_name[0] = '\0';
              s_sh->bt_pairing_dirty = true;
              xSemaphoreGive(s_sh->mutex);
            }
          } else if (*p == '1' && p[1] == ',') {
            const char *name_in = p + 2;
            char nameBuf[BT_PEER_NAME_MAX];
            sb1_sanitize_peer_name(nameBuf, sizeof(nameBuf), name_in);
            if (s_sh && s_sh->mutex && xSemaphoreTake(s_sh->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
              s_sh->bt_peer_connected = true;
              strncpy(s_sh->bt_peer_name, nameBuf, BT_PEER_NAME_MAX - 1);
              s_sh->bt_peer_name[BT_PEER_NAME_MAX - 1] = '\0';
              s_sh->bt_pairing_dirty = true;
              xSemaphoreGive(s_sh->mutex);
            }
          }
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
