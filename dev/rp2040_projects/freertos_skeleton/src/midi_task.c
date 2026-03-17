/*
 * MIDI task: notified by button ISR; sends USB MIDI note on/off and updates shared state.
 * Uses TinyUSB: tud_mounted() for USB status; if CFG_TUD_MIDI is set, tud_midi_stream_write.
 * With SDK stdio USB only (no MIDI interface), we update state and printf for demo.
 */
#include "config.h"
#include "midi_task.h"
#include "hardware/gpio.h"
#include "tusb.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

static void midi_task_fn(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;
  uint32_t notif_value;

  for (;;) {
    if (xTaskNotifyWait(0, 0xFFFFFFFF, &notif_value, pdMS_TO_TICKS(10)) == pdTRUE) {
      vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
      int state = gpio_get(BUTTON_PIN) ? 1 : 0; /* 0 = pressed (pull-up) */

      bool mounted = tud_mounted();
      if (mounted) {
        if (state == 0) {
#if CFG_TUD_MIDI
          uint8_t note_on[3] = { 0x90 | (MIDI_CH - 1), MIDI_NOTE, MIDI_VEL };
          tud_midi_stream_write(0, note_on, 3);
#endif
          if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            *sh->button_state = 0;
            sh->usb_mounted = true;
            snprintf(sh->last_event, LAST_EVENT_LEN, "NOTE ON C4");
            xSemaphoreGive(sh->mutex);
          }
        } else {
#if CFG_TUD_MIDI
          uint8_t note_off[3] = { 0x80 | (MIDI_CH - 1), MIDI_NOTE, 0 };
          tud_midi_stream_write(0, note_off, 3);
#endif
          if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            *sh->button_state = 1;
            sh->usb_mounted = true;
            snprintf(sh->last_event, LAST_EVENT_LEN, "NOTE OFF C4");
            xSemaphoreGive(sh->mutex);
          }
        }
      } else if (sh && sh->mutex && sh->button_state) {
        if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          *sh->button_state = state;
          sh->usb_mounted = false;
          xSemaphoreGive(sh->mutex);
        }
      }
    }

#if CFG_TUD_MIDI
    while (tud_midi_available()) {
      uint8_t packet[4];
      tud_midi_packet_read(packet);
    }
#endif
  }
}

TaskHandle_t midi_task_create(shared_state_t *shared) {
  TaskHandle_t handle = NULL;
  xTaskCreate(midi_task_fn, "midi", MIDI_TASK_STACK_SIZE, shared, MIDI_TASK_PRIORITY, &handle);
  return handle;
}
