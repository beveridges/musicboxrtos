/*
 * RP2040 MIDI Button + NeoPixel — FreeRTOS (converted from Arduino RP2040_MIDI_5110_RTOS).
 *
 * Button (GPIO 12) -> ISR notifies MIDI task -> USB MIDI note on/off + shared state.
 * UI task -> NeoPixel (GPIO 16): idle=off, pressed=white.
 * Display task -> printf USB status and last event (serial).
 *
 * Build: ./build_uf2.sh
 */
#include "pico/stdlib.h"
#include "config.h"
#include "button_isr.h"
#include "midi_task.h"
#include "ui_task.h"
#include "display_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

static int s_button_state = 1; /* HIGH = not pressed */
static SemaphoreHandle_t s_mutex = NULL;
static shared_state_t s_shared;

int main(void) {
  stdio_init_all();

  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    for (;;) tight_loop_contents();
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
    for (;;) tight_loop_contents();
  }

  sleep_ms(1500);

  button_isr_attach(midi_handle);

  vTaskStartScheduler();

  for (;;) tight_loop_contents();
  return 0;
}
