/*
 * button_isr.cpp
 *
 * ISR must stay extremely short: read pin, notify task, yield. No library calls.
 * No volatile: we pass state via notification value; no shared variable read by ISR.
 */

#include "config.h"
#include "event_types.h"
#include "button_isr.h"
#include <Arduino.h>

static TaskHandle_t s_midi_task = NULL;

static void on_button_change(void) {
  if (s_midi_task == NULL) return;
  BaseType_t woken = pdFALSE;
  uint32_t value = (digitalRead(BUTTON_PIN) == LOW) ? BUTTON_PRESSED : BUTTON_RELEASED;
  xTaskNotifyFromISR(s_midi_task, value, eSetValueWithOverwrite, &woken);
  portYIELD_FROM_ISR(woken);
}

void button_isr_set_midi_task(TaskHandle_t midi_task) {
  s_midi_task = midi_task;
}

void button_isr_attach(void) {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), on_button_change, CHANGE);
}
