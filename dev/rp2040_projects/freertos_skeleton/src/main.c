/*
 * test_sketch_usb_midi_button.ino port — FreeRTOS on RP2040.
 * Buttons: GPIO2–5 — BL select (UI), BR/TL/TR MIDI test notes.
 * UI task -> discrete blue LEDs (GPIO6/7/8/9). Display task -> Nokia 5110.
 * Build: ./build_uf2.sh
 */
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "config.h"
#include "gpio_led.h"
#include "midi_task.h"
#include "ui_task.h"
#include "display_task.h"
#include "pot_task.h"
#include "usb_task.h"
#include "tusb.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

static int s_button_state = 1;
static SemaphoreHandle_t s_mutex = NULL;
static shared_state_t s_shared;

static void buttons_hw_init(void) {
  const uint pins[BTN_PANEL_COUNT] = {
      BTN_SELECT_GPIO,
      BTN_MIDI_A_GPIO,
      BTN_MIDI_B_GPIO,
      BTN_MIDI_C_GPIO,
  };
  for (unsigned i = 0; i < BTN_PANEL_COUNT; i++) {
    gpio_init(pins[i]);
    gpio_set_dir(pins[i], GPIO_IN);
    gpio_pull_up(pins[i]);
  }
}

static void startup_task_fn(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;
  vTaskDelay(pdMS_TO_TICKS(1500));
  if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    bool mounted = tud_mounted();
    sh->usb_mounted = mounted;
    if (!sh->menu_active) {
      if (mounted) {
        snprintf(sh->line4, LINE_LEN, "MIDI READY");
        snprintf(sh->line5, LINE_LEN, "BUTTON TEST");
      } else {
        snprintf(sh->line4, LINE_LEN, "NOT MOUNTED");
        snprintf(sh->line5, LINE_LEN, "CHECK USB STACK");
      }
    }
    xSemaphoreGive(sh->mutex);
  }
  vTaskDelete(NULL);
}

int main(void) {
  stdio_init_all();

  gpio_led_init();
  buttons_hw_init();

  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    for (;;) tight_loop_contents();
  }
  s_shared.button_state = &s_button_state;
  s_shared.pot_raw = 0;
  s_shared.pot_filtered_raw = 0;
  s_shared.pot_cc_127 = 0;
  s_shared.pot_step = 0;
  s_shared.pot_quant_step = 0;
  s_shared.ui_rotate_events = 0;
  s_shared.ui_rotate_events_last_sec = 0;
  s_shared.mutex = s_mutex;
  s_shared.usb_mounted = false;
  s_shared.menu_active = true;
  s_shared.menu_dirty = false;
  s_shared.menu_sel = 0;
  s_shared.menu_invert_row = 0xFFu;
  s_shared.midi_channel = 1;
  s_shared.program_number = 0;
  s_shared.cc_number = POT_MIDI_CC;
  s_shared.tap_bpm = 120;
  s_shared.arp_enabled = false;
  s_shared.arp_rate = 8;
  s_shared.program_change_pending = false;
  s_shared.midi_btn_live = 0;
  memset(s_shared.menu_line, 0, sizeof(s_shared.menu_line));
  strncpy(s_shared.last_event, "---", LAST_EVENT_LEN - 1);
  s_shared.last_event[LAST_EVENT_LEN - 1] = '\0';
  snprintf(s_shared.line4, LINE_LEN, "USB MIDI INIT");
  snprintf(s_shared.line5, LINE_LEN, "WAIT PC ENUM");

  usb_task_create();
  ui_task_create(&s_shared);
  pot_task_create(&s_shared);
  display_task_create(&s_shared);

  TaskHandle_t midi_handle = midi_task_create(&s_shared);
  if (midi_handle == NULL) {
    for (;;) tight_loop_contents();
  }

  xTaskCreate(startup_task_fn, "startup", 128, &s_shared, 1, NULL);

  vTaskStartScheduler();

  for (;;) tight_loop_contents();
  return 0;
}
