#include "config.h"
#include "button_isr.h"
#include <Arduino.h>

static TaskHandle_t s_midi_task = NULL;

static void on_button_change(void) {
  if (s_midi_task == NULL) return;
  BaseType_t woken = pdFALSE;
  uint32_t value = (digitalRead(BUTTON_PIN) == LOW) ? 1 : 0;
  xTaskNotifyFromISR(s_midi_task, value, eSetValueWithOverwrite, &woken);
  portYIELD_FROM_ISR(woken);
}

void button_isr_attach(TaskHandle_t midi_task) {
  s_midi_task = midi_task;
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), on_button_change, CHANGE);
}
