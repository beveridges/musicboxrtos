/*
 * main.ino — RP2040 Zero USB MIDI (FreeRTOS)
 *
 * Milestone 1: one button (GPIO12), one MIDI note over USB, NeoPixel status,
 * ISR → MIDI task, UI task for LED. No display. Tools -> OS -> FreeRTOS SMP.
 */

#include "config.h"
#include "button_isr.h"
#include "midi_task.h"
#include "ui_task.h"
#include <task.h>

static int s_button_state = HIGH;
static SemaphoreHandle_t s_mutex = NULL;
static shared_button_t s_shared;

void setup() {
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    while (1) delay(1000);
  }
  s_shared.button_state = &s_button_state;
  s_shared.mutex = s_mutex;

  ui_task_create(&s_shared);

  TaskHandle_t midi_handle = midi_task_create(&s_shared);
  if (midi_handle == NULL) {
    while (1) delay(1000);
  }

  delay(1500);

  button_isr_attach(midi_handle);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
