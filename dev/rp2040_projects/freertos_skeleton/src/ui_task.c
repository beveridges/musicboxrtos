/*
 * UI input + event queue + menu FSM processing.
 * Connectivity UI (hold Select to enter): pot + short Select; long-hold Select to exit (LED sequence).
 */
#include "config.h"
#include "menu.h"
#include "ui_task.h"
#include "sb1_setup.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "gpio_led.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <stdio.h>

typedef enum {
  PAIR_UI_NONE = 0,
  /* Select down but < MENU_LONG_PRESS_MS: BL mirrors button; menu short/long on release. */
  PAIR_UI_PRE_ENTRY,
  PAIR_UI_ENTRY_HOLD, /* Pairing LED sequence — not in pairing yet */
  PAIR_UI_EXIT_HOLD,  /* Leave connectivity UI — must be holding through LED sequence */
  /* In connectivity UI: short tap = confirm; hold = exit gesture (same threshold as menu long). */
  PAIR_UI_CONN_PRESS,
} pair_ui_t;

static void pair_ui_apply_entry_leds(uint32_t elapsed_ms) {
  if (elapsed_ms < PAIR_LED_FIRST_MS) {
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, false);
    gpio_led_set(LED_BLUE_SELECT_GPIO, false);
  } else if (elapsed_ms < PAIR_LED_FIRST_MS + PAIR_LED_STEP_MS) {
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, false);
    gpio_led_set(LED_BLUE_SELECT_GPIO, false);
  } else if (elapsed_ms < PAIR_LED_FIRST_MS + 2u * PAIR_LED_STEP_MS) {
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, false);
    gpio_led_set(LED_BLUE_SELECT_GPIO, false);
  } else if (elapsed_ms < PAIR_LED_FIRST_MS + 3u * PAIR_LED_STEP_MS) {
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, true);
    gpio_led_set(LED_BLUE_SELECT_GPIO, false);
  } else if (elapsed_ms < PAIR_ENTRY_ALL_OFF_MS) {
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, true);
    gpio_led_set(LED_BLUE_SELECT_GPIO, true);
  } else {
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, false);
    gpio_led_set(LED_BLUE_SELECT_GPIO, false);
  }
}

static void pair_ui_apply_exit_leds(uint32_t elapsed_ms) {
  if (elapsed_ms < PAIR_LED_FIRST_MS) {
    gpio_led_set(LED_BLUE_SELECT_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, true);
  } else if (elapsed_ms < PAIR_LED_FIRST_MS + PAIR_LED_STEP_MS) {
    gpio_led_set(LED_BLUE_SELECT_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, true);
  } else if (elapsed_ms < PAIR_LED_FIRST_MS + 2u * PAIR_LED_STEP_MS) {
    gpio_led_set(LED_BLUE_SELECT_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, false);
  } else if (elapsed_ms < PAIR_LED_FIRST_MS + 3u * PAIR_LED_STEP_MS) {
    gpio_led_set(LED_BLUE_SELECT_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, false);
  } else {
    gpio_led_set(LED_BLUE_SELECT_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, false);
  }
}

static void pair_ui_clear_all_leds(void) {
  gpio_led_set(LED_BLUE_SELECT_GPIO, false);
  gpio_led_set(LED_BLUE_MIDI_B_GPIO, false);
  gpio_led_set(LED_BLUE_MIDI_C_GPIO, false);
  gpio_led_set(LED_BLUE_MIDI_A_GPIO, false);
}

static void setup_set_ui_hold_active(shared_state_t *sh, bool on) {
  if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    sh->ui_setup_hold_active = on;
    xSemaphoreGive(sh->mutex);
  }
}

static void pair_ui_complete_entry(shared_state_t *sh) {
  sb1_enter_setup_mode();
  if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    sh->menu_parent_preview = false;
    sh->menu_active = false;
    sh->bt_pairing_active = true;
    sh->connectivity_screen = SB1_CONN_SCREEN_ROOT;
    sh->connectivity_sel = 0;
    sh->connectivity_qr_visible = false;
    sh->connectivity_qr_wifi = false;
    sh->connectivity_connecting_bt = false;
    sh->connectivity_connecting_wifi = false;
    sh->bt_pairing_dirty = true;
    xSemaphoreGive(sh->mutex);
  }
}

static void pair_ui_complete_exit(shared_state_t *sh) {
  if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    sh->bt_pairing_active = false;
    sh->connectivity_screen = SB1_CONN_SCREEN_ROOT;
    sh->connectivity_sel = 0;
    sh->connectivity_qr_visible = false;
    sh->connectivity_qr_wifi = false;
    sh->connectivity_connecting_bt = false;
    sh->connectivity_connecting_wifi = false;
    sh->bt_pairing_dirty = true;
    menu_init(sh);
    menu_render(sh);
    xSemaphoreGive(sh->mutex);
  }
}

static void connectivity_ui_process_event(shared_state_t *sh, const ui_event_t *ev) {
  if (!sh || !ev) {
    return;
  }
  if (ev->type == EV_BUTTON_LONG) {
    if (sh->connectivity_qr_visible) {
      sh->connectivity_qr_visible = false;
      sh->bt_pairing_dirty = true;
      return;
    }
    if (sh->connectivity_screen == SB1_CONN_SCREEN_ROOT) {
      return;
    }
    {
      uint8_t was = sh->connectivity_screen;
      sh->connectivity_screen = SB1_CONN_SCREEN_ROOT;
      sh->connectivity_sel = (was == SB1_CONN_SCREEN_BT) ? 0u : 1u;
      sh->connectivity_connecting_bt = false;
      sh->connectivity_connecting_wifi = false;
      sh->bt_pairing_dirty = true;
    }
    return;
  }
  if (ev->type == EV_ROTATE && ev->delta != 0) {
    if (sh->connectivity_qr_visible) {
      return;
    }
    int s = (int)sh->connectivity_sel + (int)ev->delta;
    if (s < 0) {
      s = 0;
    }
    if (s > 1) {
      s = 1;
    }
    sh->connectivity_sel = (uint8_t)s;
    sh->bt_pairing_dirty = true;
    return;
  }
  if (ev->type != EV_BUTTON_SHORT) {
    return;
  }
  if (sh->connectivity_qr_visible) {
    sh->connectivity_qr_visible = false;
    sh->bt_pairing_dirty = true;
    return;
  }
  switch (sh->connectivity_screen) {
    case SB1_CONN_SCREEN_ROOT:
      sh->connectivity_screen = sh->connectivity_sel == 0 ? SB1_CONN_SCREEN_BT : SB1_CONN_SCREEN_WIFI;
      sh->connectivity_sel = 0;
      sh->bt_pairing_dirty = true;
      return;
    case SB1_CONN_SCREEN_BT:
      if (sh->connectivity_sel == 0) {
        if (!sh->bt_peer_connected) {
          sh->connectivity_connecting_bt = true;
        }
      } else {
        sh->connectivity_qr_visible = true;
        sh->connectivity_qr_wifi = false;
      }
      sh->bt_pairing_dirty = true;
      return;
    case SB1_CONN_SCREEN_WIFI:
      if (sh->connectivity_sel == 0) {
        if (!sh->wifi_sta_connected) {
          sh->connectivity_connecting_wifi = true;
        }
      } else {
        sh->connectivity_qr_visible = true;
        sh->connectivity_qr_wifi = true;
      }
      sh->bt_pairing_dirty = true;
      return;
    default:
      sh->connectivity_screen = SB1_CONN_SCREEN_ROOT;
      sh->connectivity_sel = 0;
      sh->bt_pairing_dirty = true;
      return;
  }
}

static void ui_task_fn(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;
  QueueHandle_t q = xQueueCreate(UI_EVENT_QUEUE_LEN, sizeof(ui_event_t));
  bool was_down = false;
  uint32_t down_ms = 0;
  int last_step = -1;
  TickType_t last_rotate_tick = 0;
  TickType_t telemetry_tick = xTaskGetTickCount();
  TickType_t events_window_tick = telemetry_tick;
  uint16_t events_window = 0;

  pair_ui_t pair_ui = PAIR_UI_NONE;
  bool s_suppress_next_release = false;
  bool s_entry_gesture_done = false;
  bool s_exit_gesture_done = false;
  uint32_t s_sel_press_ms = 0;   /* Select down edge (menu timing + pre-entry) */
  uint32_t s_pair_led_t0 = 0;    /* Start of pairing entry LED timeline */

  menu_init(sh);
  if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    sh->ui_setup_hold_active = false;
    menu_render(sh);
    xSemaphoreGive(sh->mutex);
  }

  for (;;) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool bt_pairing = false;
    /* CONNECTIONS root: hold >= MENU_LONG_PRESS_MS starts LED exit. Submenus / QR: same threshold on
     * release -> EV_BUTTON_LONG (back), same idea as main menu LONG=BACK. */
    bool conn_exit_hold_armed = false;
    if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      bt_pairing = sh->bt_pairing_active;
      conn_exit_hold_armed =
          bt_pairing && sh->connectivity_screen == SB1_CONN_SCREEN_ROOT && !sh->connectivity_qr_visible;
      xSemaphoreGive(sh->mutex);
    }

    bool down = (gpio_get(BTN_SELECT_GPIO) == 0);

    if (pair_ui == PAIR_UI_EXIT_HOLD) {
      setup_set_ui_hold_active(sh, true);
      if (down && was_down) {
        uint32_t el = now - down_ms;
        pair_ui_apply_exit_leds(el);
        if (el >= PAIR_EXIT_ALL_OFF_MS && !s_exit_gesture_done) {
          s_exit_gesture_done = true;
          pair_ui_complete_exit(sh);
          pair_ui = PAIR_UI_NONE;
          setup_set_ui_hold_active(sh, false);
          s_suppress_next_release = true;
        }
      } else if (!down && was_down) {
        if (!s_exit_gesture_done) {
          pair_ui_clear_all_leds();
        }
        pair_ui = PAIR_UI_NONE;
        setup_set_ui_hold_active(sh, false);
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(UI_POLL_MS));
        continue;
      }
    } else if (pair_ui == PAIR_UI_PRE_ENTRY) {
      if (!down && was_down) {
        uint32_t dur = now - s_sel_press_ms;
        pair_ui = PAIR_UI_NONE;
        if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          if (sh->menu_parent_preview) {
            sh->menu_parent_preview = false;
            sh->menu_dirty = true;
          }
          xSemaphoreGive(sh->mutex);
        }
        if (s_suppress_next_release) {
          s_suppress_next_release = false;
        } else if (dur >= DEBOUNCE_MS) {
          ui_event_t ev = {.type = (dur >= MENU_LONG_PRESS_MS) ? EV_BUTTON_LONG : EV_BUTTON_SHORT, .delta = 0};
          xQueueSend(q, &ev, 0);
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(UI_POLL_MS));
        continue;
      }
      if (down && was_down && (uint32_t)(now - s_sel_press_ms) >= MENU_LONG_PRESS_MS) {
        bool go_entry_hold = true;
        if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          bool menu_on = sh->menu_active;
          bool esc = sh->menu_esc_available;
          if (menu_on && !bt_pairing && esc) {
            go_entry_hold = false;
            /* Long-back should trigger at threshold, not on release. */
            if (sh->menu_parent_preview) {
              sh->menu_parent_preview = false;
              sh->menu_dirty = true;
            }
          } else {
            if (sh->menu_parent_preview) {
              sh->menu_parent_preview = false;
              sh->menu_dirty = true;
            }
          }
          xSemaphoreGive(sh->mutex);
        }
        if (go_entry_hold) {
          pair_ui = PAIR_UI_ENTRY_HOLD;
          s_pair_led_t0 = now;
          setup_set_ui_hold_active(sh, true);
          pair_ui_apply_entry_leds(0);
        } else {
          ui_event_t ev = {.type = EV_BUTTON_LONG, .delta = 0};
          xQueueSend(q, &ev, 0);
          pair_ui = PAIR_UI_NONE;
          s_suppress_next_release = true;
        }
      }
    } else if (pair_ui == PAIR_UI_ENTRY_HOLD) {
      setup_set_ui_hold_active(sh, true);
      gpio_led_set(LED_BLUE_SELECT_GPIO, false);
      if (down && was_down) {
        uint32_t el = now - s_pair_led_t0;
        pair_ui_apply_entry_leds(el);
        if (el >= PAIR_ENTRY_ALL_OFF_MS && !s_entry_gesture_done) {
          s_entry_gesture_done = true;
          pair_ui_complete_entry(sh);
          pair_ui = PAIR_UI_NONE;
          setup_set_ui_hold_active(sh, false);
          s_suppress_next_release = true;
        }
      } else if (!down && was_down) {
        uint32_t dur = now - s_sel_press_ms;
        if (!s_entry_gesture_done) {
          pair_ui_clear_all_leds();
          setup_set_ui_hold_active(sh, false);
          pair_ui = PAIR_UI_NONE;
          if (s_suppress_next_release) {
            s_suppress_next_release = false;
          } else if (dur >= DEBOUNCE_MS) {
            ui_event_t ev = {.type = (dur >= MENU_LONG_PRESS_MS) ? EV_BUTTON_LONG : EV_BUTTON_SHORT, .delta = 0};
            xQueueSend(q, &ev, 0);
          }
        } else {
          pair_ui = PAIR_UI_NONE;
          if (s_suppress_next_release) {
            s_suppress_next_release = false;
          }
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(UI_POLL_MS));
        continue;
      }
    } else if (pair_ui == PAIR_UI_CONN_PRESS) {
      if (!down && was_down) {
        pair_ui = PAIR_UI_NONE;
        uint32_t dur = now - s_sel_press_ms;
        if (s_suppress_next_release) {
          s_suppress_next_release = false;
        } else if (dur >= DEBOUNCE_MS) {
          ui_event_t ev = {.type = (dur >= MENU_LONG_PRESS_MS) ? EV_BUTTON_LONG : EV_BUTTON_SHORT, .delta = 0};
          xQueueSend(q, &ev, 0);
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(UI_POLL_MS));
        continue;
      }
      if (down && was_down) {
        if (conn_exit_hold_armed && (uint32_t)(now - s_sel_press_ms) >= MENU_LONG_PRESS_MS) {
          pair_ui = PAIR_UI_EXIT_HOLD;
          down_ms = now;
          s_exit_gesture_done = false;
          setup_set_ui_hold_active(sh, true);
          pair_ui_apply_exit_leds(0);
        } else if (!conn_exit_hold_armed && (uint32_t)(now - s_sel_press_ms) >= MENU_LONG_PRESS_MS) {
          ui_event_t ev = {.type = EV_BUTTON_LONG, .delta = 0};
          xQueueSend(q, &ev, 0);
          pair_ui = PAIR_UI_NONE;
          s_suppress_next_release = true;
        }
      }
    } else {
      /* PAIR_UI_NONE */
      if (down && !was_down) {
        down_ms = now;
        was_down = true;
        s_entry_gesture_done = false;
        s_exit_gesture_done = false;
        if (bt_pairing) {
          pair_ui = PAIR_UI_CONN_PRESS;
          s_sel_press_ms = now;
        } else {
          pair_ui = PAIR_UI_PRE_ENTRY;
          s_sel_press_ms = now;
        }
      } else if (!down && was_down) {
        uint32_t dur = now - down_ms;
        if (s_suppress_next_release) {
          s_suppress_next_release = false;
        } else if (dur >= DEBOUNCE_MS) {
          ui_event_t ev = {.type = (dur >= MENU_LONG_PRESS_MS) ? EV_BUTTON_LONG : EV_BUTTON_SHORT, .delta = 0};
          xQueueSend(q, &ev, 0);
        }
      }
    }

    /* BL: mirror Select when useful; connectivity mode uses CONN_PRESS for tap vs long-exit. */
    if (pair_ui == PAIR_UI_NONE) {
      if (bt_pairing || s_suppress_next_release) {
        gpio_led_set(LED_BLUE_SELECT_GPIO, false);
      } else {
        gpio_led_set(LED_BLUE_SELECT_GPIO, down);
      }
    } else if (pair_ui == PAIR_UI_PRE_ENTRY) {
      gpio_led_set(LED_BLUE_SELECT_GPIO, down);
    } else if (pair_ui == PAIR_UI_CONN_PRESS) {
      gpio_led_set(LED_BLUE_SELECT_GPIO, down);
    }

    if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      int step = sh->pot_step;
      bool menu_on = sh->menu_active;
      bool conn_ui = sh->bt_pairing_active;
      menu_process_midi_buttons(sh, sh->midi_btn_live);
      if (last_step < 0) {
        last_step = step;
      } else if ((menu_on || conn_ui) && step != last_step) {
        TickType_t now_tick = xTaskGetTickCount();
        if ((now_tick - last_rotate_tick) >= pdMS_TO_TICKS(UI_ROTATE_MIN_EVENT_MS)) {
          int diff = step - last_step;
          if (diff > 0) {
            ui_event_t ev = {.type = EV_ROTATE, .delta = 1};
            xQueueSend(q, &ev, 0);
            events_window++;
          } else {
            ui_event_t ev = {.type = EV_ROTATE, .delta = -1};
            xQueueSend(q, &ev, 0);
            events_window++;
          }
          last_rotate_tick = now_tick;
        }
        last_step = step;
      }
      xSemaphoreGive(sh->mutex);
    }

    for (;;) {
      ui_event_t ev;
      if (xQueueReceive(q, &ev, 0) != pdTRUE) break;
      if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (sh->bt_pairing_active) {
          connectivity_ui_process_event(sh, &ev);
        } else {
          menu_process_event(sh, &ev);
          if (sh->menu_active) {
            menu_render(sh);
          }
        }
        if (ev.type == EV_ROTATE) {
          sh->ui_rotate_events++;
        }
        xSemaphoreGive(sh->mutex);
      }
    }

    TickType_t now_tick = xTaskGetTickCount();
    if ((now_tick - events_window_tick) >= pdMS_TO_TICKS(1000)) {
      if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        sh->ui_rotate_events_last_sec = events_window;
        xSemaphoreGive(sh->mutex);
      }
      events_window = 0;
      events_window_tick = now_tick;
    }
    if ((now_tick - telemetry_tick) >= pdMS_TO_TICKS(UI_TELEMETRY_PRINT_MS)) {
      if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        uint8_t mb = sh->midi_btn_live;
        if (sb1_is_stdio_ready()) {
          printf("POT raw=%u filt=%u cc=%u step=%u ev1s=%u total=%lu  BTN345=%c%c%c\n",
                 (unsigned)sh->pot_raw,
                 (unsigned)sh->pot_filtered_raw,
                 (unsigned)sh->pot_cc_127,
                 (unsigned)sh->pot_quant_step,
                 (unsigned)sh->ui_rotate_events_last_sec,
                 (unsigned long)sh->ui_rotate_events,
                 (mb & 1u) ? '1' : '0',
                 (mb & 2u) ? '1' : '0',
                 (mb & 4u) ? '1' : '0');
        }
        xSemaphoreGive(sh->mutex);
      }
      telemetry_tick = now_tick;
    }

    was_down = down;
    vTaskDelay(pdMS_TO_TICKS(UI_POLL_MS));
  }
}

void ui_task_create(shared_state_t *shared) {
  BaseType_t ok = xTaskCreate(ui_task_fn, "ui", UI_TASK_STACK_SIZE, shared, UI_TASK_PRIORITY, NULL);
  if (ok != pdPASS) {
    for (;;) {
      tight_loop_contents();
    }
  }
}
