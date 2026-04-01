/*
 * UI input + event queue + menu FSM processing.
 * Long-hold on Select (3s = 1s/phase): TL @ 1s, +TR @ 2s, +BR @ 3s, 50ms hold, off, 1s all-4 flash, sb1_enter_setup_mode().
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
#include "pico/time.h"
#include <stdio.h>

typedef enum {
  SETUP_ST_NONE = 0,
  SETUP_ST_HOLDING,
  SETUP_ST_P3_SHOW,
  SETUP_ST_FLASH,
} setup_st_t;

static void setup_apply_progress_leds(uint32_t elapsed_ms) {
  if (elapsed_ms < SETUP_HOLD_PHASE1_MS) {
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, false);
  } else if (elapsed_ms < SETUP_HOLD_PHASE2_MS) {
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, false);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, false);
  } else if (elapsed_ms < SETUP_HOLD_TOTAL_MS) {
    gpio_led_set(LED_BLUE_MIDI_B_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_C_GPIO, true);
    gpio_led_set(LED_BLUE_MIDI_A_GPIO, false);
  }
}

static void setup_clear_progress_leds(void) {
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

  setup_st_t s_setup = SETUP_ST_NONE;
  uint32_t s_aux_t0 = 0;
  bool s_suppress_next_release = false;

  menu_init(sh);
  if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    sh->ui_setup_hold_active = false;
    menu_render(sh);
    xSemaphoreGive(sh->mutex);
  }

  for (;;) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    /* Timers for P3 show + flash run even if button released mid-sequence */
    if (s_setup == SETUP_ST_P3_SHOW) {
      setup_set_ui_hold_active(sh, true);
      if ((uint32_t)(now - s_aux_t0) >= SETUP_PHASE3_SHOW_MS) {
        setup_clear_progress_leds();
        gpio_led_set(LED_BLUE_SELECT_GPIO, true);
        gpio_led_set(LED_BLUE_MIDI_B_GPIO, true);
        gpio_led_set(LED_BLUE_MIDI_C_GPIO, true);
        gpio_led_set(LED_BLUE_MIDI_A_GPIO, true);
        s_setup = SETUP_ST_FLASH;
        s_aux_t0 = now;
      }
    }
    if (s_setup == SETUP_ST_FLASH) {
      setup_set_ui_hold_active(sh, true);
      if ((uint32_t)(now - s_aux_t0) >= SETUP_SUCCESS_FLASH_MS) {
        gpio_led_set(LED_BLUE_SELECT_GPIO, false);
        gpio_led_set(LED_BLUE_MIDI_B_GPIO, false);
        gpio_led_set(LED_BLUE_MIDI_C_GPIO, false);
        gpio_led_set(LED_BLUE_MIDI_A_GPIO, false);
        sb1_enter_setup_mode();
        s_setup = SETUP_ST_NONE;
        s_suppress_next_release = true;
        setup_set_ui_hold_active(sh, false);
      }
    }

    bool down = (gpio_get(BTN_SELECT_GPIO) == 0);

    if (s_setup == SETUP_ST_NONE || s_setup == SETUP_ST_HOLDING) {
      if (down && !was_down) {
        down_ms = now;
        was_down = true;
        s_setup = SETUP_ST_HOLDING;
      } else if (!down && was_down) {
        uint32_t dur = now - down_ms;
        was_down = false;
        if (s_suppress_next_release) {
          s_suppress_next_release = false;
        } else if (s_setup == SETUP_ST_HOLDING) {
          setup_clear_progress_leds();
          setup_set_ui_hold_active(sh, false);
          s_setup = SETUP_ST_NONE;
          if (dur >= DEBOUNCE_MS) {
            ui_event_t ev = {.type = (dur >= MENU_LONG_PRESS_MS) ? EV_BUTTON_LONG : EV_BUTTON_SHORT, .delta = 0};
            xQueueSend(q, &ev, 0);
          }
        }
      }

      if (down && was_down && s_setup == SETUP_ST_HOLDING) {
        uint32_t el = now - down_ms;
        setup_set_ui_hold_active(sh, true);
        gpio_led_set(LED_BLUE_SELECT_GPIO, false);
        if (el >= SETUP_HOLD_TOTAL_MS) {
          gpio_led_set(LED_BLUE_MIDI_B_GPIO, true);
          gpio_led_set(LED_BLUE_MIDI_C_GPIO, true);
          gpio_led_set(LED_BLUE_MIDI_A_GPIO, true);
          s_setup = SETUP_ST_P3_SHOW;
          s_aux_t0 = now;
        } else {
          setup_apply_progress_leds(el);
        }
      }
    } else {
      /* P3_SHOW or FLASH: still track was_down for edge bookkeeping */
      if (down && !was_down) {
        down_ms = now;
        was_down = true;
      } else if (!down && was_down) {
        was_down = false;
        if (s_setup == SETUP_ST_P3_SHOW) {
          setup_clear_progress_leds();
          gpio_led_set(LED_BLUE_SELECT_GPIO, false);
          s_setup = SETUP_ST_NONE;
          setup_set_ui_hold_active(sh, false);
          uint32_t dur = now - down_ms;
          if (dur >= DEBOUNCE_MS) {
            ui_event_t ev = {.type = (dur >= MENU_LONG_PRESS_MS) ? EV_BUTTON_LONG : EV_BUTTON_SHORT, .delta = 0};
            xQueueSend(q, &ev, 0);
          }
        } else if (s_setup == SETUP_ST_FLASH) {
          if (s_suppress_next_release) {
            s_suppress_next_release = false;
          }
        }
      }
    }

    /* BL mirrors Select only when not in setup ramp (BL stays off during hold). */
    if (s_setup == SETUP_ST_NONE) {
      gpio_led_set(LED_BLUE_SELECT_GPIO, down);
    }

    if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      int step = sh->pot_step;
      bool menu_on = sh->menu_active;
      if (last_step < 0) {
        last_step = step;
      } else if (menu_on && step != last_step) {
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
        menu_process_event(sh, &ev);
        if (sh->menu_active) {
          menu_render(sh);
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
        xSemaphoreGive(sh->mutex);
      }
      telemetry_tick = now_tick;
    }

    vTaskDelay(pdMS_TO_TICKS(UI_POLL_MS));
  }
}

void ui_task_create(shared_state_t *shared) {
  xTaskCreate(ui_task_fn, "ui", UI_TASK_STACK_SIZE, shared, UI_TASK_PRIORITY, NULL);
}
