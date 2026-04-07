/*
 * Display task: Nokia 5110 (PCD8544) — menu mode (6 lines) or dashboard + pot.
 */
#include "config.h"
#include "display_task.h"
#include "pcd8544.h"
#include "qrcodegen.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

#define SB1_QR_BUF_LEN 512

static uint8_t s_qr_temp[SB1_QR_BUF_LEN];
static uint8_t s_qr_data[SB1_QR_BUF_LEN];
static int s_qr_modules;
static bool s_qr_ready;
static bool s_qr_session_open;

/* Up to four 14-column rows starting at row `row0` (0..5). */
static void sb1_draw_wrapped_four_lines(uint8_t row0, const char *nm) {
  size_t n = strlen(nm);
  size_t off = 0;
  for (uint8_t r = 0; r < 4u; r++) {
    char buf[16];
    size_t take = 0;
    if (off < n) {
      take = n - off;
      if (take > DISPLAY_COLS) {
        take = DISPLAY_COLS;
      }
      memcpy(buf, nm + off, take);
      off += take;
    }
    buf[take] = '\0';
    pcd8544_set_cursor(0, (uint8_t)(row0 + r));
    pcd8544_print(buf);
  }
}

/* Three 14-column rows (rows 3–5 under TURN POT TO QR). */
static void sb1_draw_wrapped_three_lines(uint8_t row0, const char *nm) {
  size_t n = strlen(nm);
  size_t off = 0;
  for (uint8_t r = 0; r < 3u; r++) {
    char buf[16];
    size_t take = 0;
    if (off < n) {
      take = n - off;
      if (take > DISPLAY_COLS) {
        take = DISPLAY_COLS;
      }
      memcpy(buf, nm + off, take);
      off += take;
    }
    buf[take] = '\0';
    pcd8544_set_cursor(0, (uint8_t)(row0 + r));
    pcd8544_print(buf);
  }
}

static void sb1_draw_bt_pairing_text(bool peer_connected, const char *peer_name) {
  pcd8544_clear();
  pcd8544_set_cursor(0, 0);
  pcd8544_print("BLUETOOTH");
  pcd8544_set_cursor(0, 1);
  pcd8544_print("PAIRING");
  if (peer_connected) {
    pcd8544_invert_row(0);
    pcd8544_invert_row(1);
  }

  if (peer_connected) {
    char combined[128];
    const char *peer = (peer_name && peer_name[0] != '\0') ? peer_name : "CONNECTED";
    snprintf(combined, sizeof combined, "%s %s", SB1_BT_DEVICE_NAME, peer);
    sb1_draw_wrapped_four_lines(2, combined);
  } else {
    pcd8544_set_cursor(0, 2);
    pcd8544_print("TURN POT TO QR");
    sb1_draw_wrapped_three_lines(3, SB1_BT_DEVICE_NAME);
  }
  pcd8544_display();
}

static void sb1_draw_qr_screen(void) {
  if (!s_qr_session_open) {
    s_qr_ready = qrcodegen_encodeText(SB1_BT_QR_PAYLOAD, s_qr_temp, s_qr_data, qrcodegen_Ecc_LOW, 1, 10,
                                      qrcodegen_Mask_AUTO, true);
    s_qr_modules = s_qr_ready ? qrcodegen_getSize(s_qr_data) : 0;
    s_qr_session_open = true;
  }

  pcd8544_clear();
  if (!s_qr_ready || s_qr_modules <= 0) {
    pcd8544_set_cursor(0, 0);
    pcd8544_print("QR ENCODE");
    pcd8544_set_cursor(0, 2);
    pcd8544_print("FAILED");
    pcd8544_display();
    return;
  }

  int size = s_qr_modules;
  int scale = 3;
  while (scale > 1 && (size * scale > PCD8544_WIDTH || size * scale > PCD8544_HEIGHT)) {
    scale--;
  }
  if (scale < 1) {
    scale = 1;
  }
  int px = size * scale;
  int ox = ((int)PCD8544_WIDTH - px) / 2;
  if (ox < 0) {
    ox = 0;
  }
  int oy = ((int)PCD8544_HEIGHT - px) / 2;
  if (oy < 0) {
    oy = 0;
  }

  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      if (!qrcodegen_getModule(s_qr_data, x, y)) {
        continue;
      }
      for (int dy = 0; dy < scale; dy++) {
        for (int dx = 0; dx < scale; dx++) {
          int pxv = ox + x * scale + dx;
          int pyv = oy + y * scale + dy;
          if (pxv >= 0 && pxv < (int)PCD8544_WIDTH && pyv >= 0 && pyv < (int)PCD8544_HEIGHT) {
            pcd8544_set_pixel((uint8_t)pxv, (uint8_t)pyv, true);
          }
        }
      }
    }
  }
  pcd8544_display();
}

static void display_task_fn(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;
  char last_line4[LINE_LEN] = "";
  char last_line5[LINE_LEN] = "";
  char last_menu[MENU_ROWS][LINE_LEN];
  uint8_t last_menu_invert = 0xFFu;
  uint16_t last_pot = 0xFFFFu;
  uint8_t last_pot_cc = 0xFFu;
  uint8_t last_midi_btn = 0xFFu;
  bool was_menu_active = false;
  static bool s_disp_bt_last = false;
  static uint8_t s_disp_bt_page = 0xFFu;
  static bool s_last_bt_peer_conn = false;
  static char s_last_bt_peer_name[BT_PEER_NAME_MAX];

  memset(last_menu, 0, sizeof(last_menu));
  s_last_bt_peer_name[0] = '\0';

  pcd8544_init(PIN_SCLK, PIN_DIN, PIN_DC, PIN_CS, PIN_RST);
  pcd8544_set_contrast(55);

  for (;;) {
    char line4[LINE_LEN] = "---";
    char line5[LINE_LEN] = "---";
    char potline[LINE_LEN];
    char menu_copy[MENU_ROWS][LINE_LEN];
    uint16_t pot = 0;
    uint8_t pot_cc = 0;
    bool menu_active = false;
    bool menu_dirty = false;
    uint8_t menu_invert_row = 0xFFu;
    uint8_t midi_btn = 0;
    bool bt_pairing = false;
    uint8_t bt_page = 0;
    bool bt_dirty = false;
    bool bt_peer_connected = false;
    char bt_peer_name[BT_PEER_NAME_MAX];
    bt_peer_name[0] = '\0';

    if (sh && sh->mutex) {
      if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        midi_btn = sh->midi_btn_live;
        strncpy(line4, sh->line4, LINE_LEN - 1);
        line4[LINE_LEN - 1] = '\0';
        strncpy(line5, sh->line5, LINE_LEN - 1);
        line5[LINE_LEN - 1] = '\0';
        pot = sh->pot_raw;
        pot_cc = sh->pot_cc_127;
        menu_active = sh->menu_active;
        menu_dirty = sh->menu_dirty;
        menu_invert_row = sh->menu_invert_row;
        memcpy(menu_copy, sh->menu_line, sizeof(menu_copy));
        bt_pairing = sh->bt_pairing_active;
        bt_page = sh->bt_pairing_page;
        bt_dirty = sh->bt_pairing_dirty;
        bt_peer_connected = sh->bt_peer_connected;
        strncpy(bt_peer_name, sh->bt_peer_name, BT_PEER_NAME_MAX - 1);
        bt_peer_name[BT_PEER_NAME_MAX - 1] = '\0';
        xSemaphoreGive(sh->mutex);
      }
    }

    if (bt_pairing) {
      bool peer_changed = (bt_peer_connected != s_last_bt_peer_conn) ||
                          (strncmp(bt_peer_name, s_last_bt_peer_name, BT_PEER_NAME_MAX) != 0);
      bool need =
          bt_dirty || !s_disp_bt_last || bt_page != s_disp_bt_page || peer_changed;
      if (need) {
        if (bt_page == 0u) {
          sb1_draw_bt_pairing_text(bt_peer_connected, bt_peer_name);
        } else {
          sb1_draw_qr_screen();
        }
        s_disp_bt_last = true;
        s_disp_bt_page = bt_page;
        s_last_bt_peer_conn = bt_peer_connected;
        strncpy(s_last_bt_peer_name, bt_peer_name, BT_PEER_NAME_MAX - 1);
        s_last_bt_peer_name[BT_PEER_NAME_MAX - 1] = '\0';
        if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          sh->bt_pairing_dirty = false;
          xSemaphoreGive(sh->mutex);
        }
      }
      was_menu_active = false;
      vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
      continue;
    }

    if (s_disp_bt_last) {
      s_disp_bt_last = false;
      s_disp_bt_page = 0xFFu;
      s_qr_session_open = false;
      s_qr_ready = false;
      s_qr_modules = 0;
    }
    snprintf(potline, LINE_LEN, "POT:%4u CC%3u", (unsigned)pot, (unsigned)pot_cc);
    char btnline[LINE_LEN];
    /* 14 cols max — * = pressed, . = open; order = GPIO3 BR, 4 TL, 5 TR */
    snprintf(btnline, LINE_LEN, "345:%c%c%c",
             (midi_btn & 1u) ? '*' : '.',
             (midi_btn & 2u) ? '*' : '.',
             (midi_btn & 4u) ? '*' : '.');

    if (menu_active) {
      /* If ui_task hasn't rendered yet, menu lines can still be empty while menu_dirty is false;
       * never skip a paint when we have nothing cached (fixes one-frame race / permanent blank). */
      bool menu_has_content = false;
      for (int i = 0; i < MENU_ROWS; i++) {
        if (menu_copy[i][0] != '\0') {
          menu_has_content = true;
          break;
        }
      }
      bool need = menu_dirty || (menu_has_content && last_menu[0][0] == '\0');
      if (!need) {
        if (menu_invert_row != last_menu_invert) {
          need = true;
        }
        for (int i = 0; i < MENU_ROWS; i++) {
          if (strcmp(menu_copy[i], last_menu[i]) != 0) {
            need = true;
            break;
          }
        }
      }
      if (need) {
        pcd8544_clear();
        for (int r = 0; r < MENU_ROWS; r++) {
          pcd8544_set_cursor(0, (uint8_t)r);
          pcd8544_print(menu_copy[r]);
        }
        if (menu_invert_row < MENU_ROWS) {
          pcd8544_invert_row(menu_invert_row);
        }
        pcd8544_display();
        memcpy(last_menu, menu_copy, sizeof(last_menu));
        last_menu_invert = menu_invert_row;
        if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          sh->menu_dirty = false;
          xSemaphoreGive(sh->mutex);
        }
        strncpy(last_line4, line4, LINE_LEN - 1);
        last_line4[LINE_LEN - 1] = '\0';
        strncpy(last_line5, line5, LINE_LEN - 1);
        last_line5[LINE_LEN - 1] = '\0';
        last_pot = pot;
        last_pot_cc = pot_cc;
      }
      was_menu_active = true;
    } else {
      bool need = strcmp(line4, last_line4) != 0 || strcmp(line5, last_line5) != 0 || pot != last_pot ||
                  pot_cc != last_pot_cc || was_menu_active || midi_btn != last_midi_btn;
      if (need) {
        strncpy(last_line4, line4, LINE_LEN - 1);
        last_line4[LINE_LEN - 1] = '\0';
        strncpy(last_line5, line5, LINE_LEN - 1);
        last_line5[LINE_LEN - 1] = '\0';
        last_pot = pot;
        last_pot_cc = pot_cc;
        last_midi_btn = midi_btn;

        pcd8544_clear();
        pcd8544_set_cursor(0, 0);
        pcd8544_print("RP2040 ZERO");
        pcd8544_set_cursor(0, 1);
        pcd8544_print(potline);
        pcd8544_set_cursor(0, 2);
        pcd8544_print(btnline);
        pcd8544_set_cursor(0, 3);
        pcd8544_print(line4);
        pcd8544_set_cursor(0, 4);
        pcd8544_print(line5);
        pcd8544_display();
      }
      was_menu_active = false;
    }

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
  }
}

void display_task_create(shared_state_t *shared) {
  BaseType_t ok =
      xTaskCreate(display_task_fn, "display", DISPLAY_TASK_STACK_SIZE, shared, DISPLAY_TASK_PRIORITY, NULL);
  if (ok != pdPASS) {
    /* Heap too small for stack, or out of task control blocks — LCD task never runs → blank screen. */
    for (;;) {
      tight_loop_contents();
    }
  }
}
