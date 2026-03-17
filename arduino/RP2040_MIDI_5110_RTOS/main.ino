/*
 * RP2040 MIDI Button + Nokia 5110 — FreeRTOS skeleton
 *
 * ISR (button) -> MIDI task (only USB MIDI sender)
 * MIDI task -> shared state (button_state, usb_mounted, last_event)
 * UI task -> NeoPixel (idle/pressed)
 * Display task -> Nokia 5110 (USB status, last note)
 *
 * Tools -> OS -> FreeRTOS SMP. Board: Waveshare RP2040 Zero (or same pins).
 */

#include "config.h"
#include "button_isr.h"
#include "midi_task.h"
#include "ui_task.h"
#include "display_task.h"
#include <task.h>
#include <cstring>

static int s_button_state = HIGH;
static SemaphoreHandle_t s_mutex = NULL;
static shared_state_t s_shared;

void setup() {
  Serial.begin(115200);
  delay(500);

  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    while (1) delay(1000);
  }
  s_shared.button_state = &s_button_state;
  s_shared.mutex = s_mutex;
  s_shared.usb_mounted = false;
  strncpy(s_shared.last_event, "---", LAST_EVENT_LEN - 1);
  s_shared.last_event[LAST_EVENT_LEN - 1] = '\0';

  ui_task_create(&s_shared);
  display_task_create(&s_shared);

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
