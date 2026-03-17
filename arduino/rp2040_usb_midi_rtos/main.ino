/*
 * main.ino — RP2040 USB MIDI button (FreeRTOS)
 *
 * Design rules:
 *   - No dynamic allocation after startup: queue and tasks created once in setup().
 *   - ISR: no MIDI/display/Serial; only task notification.
 *   - One task owns one peripheral: MIDI task = USB MIDI, UI task = NeoPixel.
 *   - Task notification for fast button signal; queue for structured UI events.
 *
 * Enable: Tools -> Operating System -> FreeRTOS SMP.
 */

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>

#include "config.h"
#include "event_types.h"
#include "button_isr.h"
#include "midi_task.h"
#include "ui_task.h"

void setup() {
  /* All allocation happens here — nothing after this. */
  QueueHandle_t ui_queue = xQueueCreate(UI_QUEUE_LEN, sizeof(ui_event_t));
  if (ui_queue == NULL) {
    while (1) { delay(1000); }
  }

  ui_task_init();
  midi_task_init();

  /* Give host time to enumerate USB. */
  delay(1500);

  TaskHandle_t midi_handle = midi_task_create(ui_queue);
  if (midi_handle == NULL) {
    while (1) { delay(1000); }
  }

  ui_task_create(ui_queue);

  button_isr_set_midi_task(midi_handle);
  button_isr_attach();
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
