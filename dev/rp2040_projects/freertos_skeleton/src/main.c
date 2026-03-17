/*
 * test_sketch_usb_midi_button.ino port — FreeRTOS on RP2040.
 * Button (GPIO 12) -> MIDI task -> USB MIDI note on/off + shared state.
 * UI task -> NeoPixel (GPIO 16). Display task -> Nokia 5110.
 * Build: ./build_uf2.sh
 */
#include "pico/stdlib.h"
#include "config.h"
#include "button_isr.h"
#include "midi_task.h"
#include "ui_task.h"
#include "display_task.h"
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

static void startup_task_fn(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;
  vTaskDelay(pdMS_TO_TICKS(1500));
  if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (tud_mounted()) {
      snprintf(sh->line4, LINE_LEN, "MIDI READY");
      snprintf(sh->line5, LINE_LEN, "BUTTON TEST");
      sh->usb_mounted = true;
    } else {
      snprintf(sh->line4, LINE_LEN, "NOT MOUNTED");
      snprintf(sh->line5, LINE_LEN, "CHECK USB STACK");
      sh->usb_mounted = false;
    }
    xSemaphoreGive(sh->mutex);
  }
  vTaskDelete(NULL);
}

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
  snprintf(s_shared.line4, LINE_LEN, "USB MIDI INIT");
  snprintf(s_shared.line5, LINE_LEN, "WAIT PC ENUM");

  usb_task_create();
  ui_task_create(&s_shared);
  display_task_create(&s_shared);

  TaskHandle_t midi_handle = midi_task_create(&s_shared);
  if (midi_handle == NULL) {
    for (;;) tight_loop_contents();
  }

  xTaskCreate(startup_task_fn, "startup", 128, &s_shared, 1, NULL);

  button_isr_attach(midi_handle);

  vTaskStartScheduler();

  for (;;) tight_loop_contents();
  return 0;
}
