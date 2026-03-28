/*
 * MIDI task: Program Change from menu; three GPIO buttons send Note On/Off (polled).
 */
#include "config.h"
#include "gpio_led.h"
#include "midi_task.h"
#include "hardware/gpio.h"
#include "tusb.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

#define MIDI_BTN_POLL_MS 5

static void midi_task_fn(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;

  /* Debounced stable state: 1 = released, 0 = pressed */
  static int s_stable[3] = {1, 1, 1};
  static uint16_t s_ctr[3] = {0};
  const uint8_t notes[3] = {BTN_MIDI_NOTE_A, BTN_MIDI_NOTE_B, BTN_MIDI_NOTE_C};
  const uint pins[3] = {BTN_MIDI_A_GPIO, BTN_MIDI_B_GPIO, BTN_MIDI_C_GPIO};
  const uint led_pins[3] = {LED_BLUE_MIDI_A_GPIO, LED_BLUE_MIDI_B_GPIO, LED_BLUE_MIDI_C_GPIO};

  for (;;) {
    bool send_pc = false;
    uint8_t pc_num = 0;
    uint8_t pc_ch = MIDI_CH;
    if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      if (sh->program_change_pending) {
        send_pc = true;
        pc_num = sh->program_number;
        pc_ch = sh->midi_channel;
        sh->program_change_pending = false;
        snprintf(sh->line4, LINE_LEN, "PC %u CH %u", (unsigned)pc_num, (unsigned)pc_ch);
        snprintf(sh->line5, LINE_LEN, "Program Sent");
      }
      xSemaphoreGive(sh->mutex);
    }
#if CFG_TUD_MIDI
    if (send_pc && tud_mounted()) {
      uint8_t msg[2] = {(uint8_t)(0xC0u | (uint8_t)(pc_ch - 1u)), pc_num};
      tud_midi_stream_write(0, msg, 2);
    }
#endif

    bool menu_active = false;
    if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      menu_active = sh->menu_active;
      xSemaphoreGive(sh->mutex);
    }

    bool mounted = tud_mounted();

    uint8_t live_bits = 0;
    for (unsigned i = 0; i < 3u; i++) {
      bool pressed = (gpio_get(pins[i]) == 0);
      if (pressed) {
        live_bits |= (uint8_t)(1u << i);
      }
      gpio_led_set(led_pins[i], pressed);
    }
    if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      sh->midi_btn_live = live_bits;
      xSemaphoreGive(sh->mutex);
    }

#if CFG_TUD_MIDI
    for (unsigned i = 0; i < 3u; i++) {
      int r = gpio_get(pins[i]) ? 1 : 0;
      if (r == s_stable[i]) {
        s_ctr[i] = 0;
      } else {
        if (s_ctr[i] < 0xFFFFu) {
          s_ctr[i]++;
        }
        if ((uint32_t)s_ctr[i] * MIDI_BTN_POLL_MS >= (uint32_t)DEBOUNCE_MS) {
          int was = s_stable[i];
          s_stable[i] = r;
          s_ctr[i] = 0;
          if (mounted && !menu_active) {
            if (was == 1 && r == 0) {
              uint8_t note_on[3] = {(uint8_t)(0x90u | (uint8_t)(MIDI_CH - 1u)), notes[i], MIDI_VEL};
              tud_midi_stream_write(0, note_on, 3);
              if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                snprintf(sh->last_event, LAST_EVENT_LEN, "NOTE ON %u", (unsigned)notes[i]);
                snprintf(sh->line4, LINE_LEN, "MIDI READY");
                snprintf(sh->line5, LINE_LEN, "NOTE ON %u", (unsigned)notes[i]);
                xSemaphoreGive(sh->mutex);
              }
            } else if (was == 0 && r == 1) {
              uint8_t note_off[3] = {(uint8_t)(0x80u | (uint8_t)(MIDI_CH - 1u)), notes[i], 0};
              tud_midi_stream_write(0, note_off, 3);
              if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                snprintf(sh->last_event, LAST_EVENT_LEN, "NOTE OFF %u", (unsigned)notes[i]);
                snprintf(sh->line4, LINE_LEN, "MIDI READY");
                snprintf(sh->line5, LINE_LEN, "NOTE OFF %u", (unsigned)notes[i]);
                xSemaphoreGive(sh->mutex);
              }
            }
          }
        }
      }
    }
#endif

    if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      int sel = gpio_get(BTN_SELECT_GPIO) ? 1 : 0;
      if (sh->button_state) {
        *sh->button_state = sel;
      }
      sh->usb_mounted = mounted;
      if (!mounted && !menu_active) {
        snprintf(sh->line4, LINE_LEN, "NOT MOUNTED");
        snprintf(sh->line5, LINE_LEN, "CHECK USB STACK");
      }
      xSemaphoreGive(sh->mutex);
    }

#if CFG_TUD_MIDI
    while (tud_midi_available()) {
      uint8_t packet[4];
      tud_midi_packet_read(packet);
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(MIDI_BTN_POLL_MS));
  }
}

TaskHandle_t midi_task_create(shared_state_t *shared) {
  TaskHandle_t handle = NULL;
  xTaskCreate(midi_task_fn, "midi", MIDI_TASK_STACK_SIZE, shared, MIDI_TASK_PRIORITY, &handle);
  return handle;
}
