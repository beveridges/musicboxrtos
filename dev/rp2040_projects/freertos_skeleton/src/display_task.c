/*
 * Display task: Nokia 5110 (PCD8544) — menu mode (6 lines) or dashboard + pot.
 */
#include "config.h"
#include "display_task.h"
#include "pcd8544.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

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

  memset(last_menu, 0, sizeof(last_menu));

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
        xSemaphoreGive(sh->mutex);
      }
    }
    snprintf(potline, LINE_LEN, "POT:%4u CC%3u", (unsigned)pot, (unsigned)pot_cc);
    char btnline[LINE_LEN];
    /* 14 cols max — * = pressed, . = open; order = GPIO3 BR, 4 TL, 5 TR */
    snprintf(btnline, LINE_LEN, "345:%c%c%c",
             (midi_btn & 1u) ? '*' : '.',
             (midi_btn & 2u) ? '*' : '.',
             (midi_btn & 4u) ? '*' : '.');

    if (menu_active) {
      bool need = menu_dirty;
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
  xTaskCreate(display_task_fn, "display", DISPLAY_TASK_STACK_SIZE, shared, DISPLAY_TASK_PRIORITY, NULL);
}
