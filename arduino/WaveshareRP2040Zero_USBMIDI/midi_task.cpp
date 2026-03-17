/*
 * midi_task.cpp — Only place that calls TinyUSB and MIDI library.
 */

#include "config.h"
#include "midi_task.h"
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <Arduino.h>

static Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

static void midi_task_fn(void* pvParameters) {
  shared_button_t* sh = (shared_button_t*)pvParameters;
  uint32_t notif_value;

  for (;;) {
    if (xTaskNotifyWait(0, 0xFFFFFFFF, &notif_value, pdMS_TO_TICKS(10)) == pdTRUE) {
      vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
      int state = digitalRead(BUTTON_PIN);

      if (TinyUSBDevice.mounted()) {
        if (state == LOW) {
          MIDI.sendNoteOn(MIDI_NOTE_C4, MIDI_VELOCITY, MIDI_CHANNEL);
        } else {
          MIDI.sendNoteOff(MIDI_NOTE_C4, 0, MIDI_CHANNEL);
        }
      }

      if (sh != NULL && sh->mutex != NULL && sh->button_state != NULL) {
        if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          *sh->button_state = state;
          xSemaphoreGive(sh->mutex);
        }
      }
    }

#ifdef TINYUSB_NEED_POLLING_TASK
    TinyUSBDevice.task();
#endif
    MIDI.read();
  }
}

TaskHandle_t midi_task_create(shared_button_t* shared) {
  static bool once;
  if (!once) {
    once = true;
    if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
    usb_midi.setStringDescriptor("RP2040 Zero MIDI");
    MIDI.begin(MIDI_CHANNEL_OMNI);
  }
  TaskHandle_t handle = NULL;
  xTaskCreate(midi_task_fn, "midi", MIDI_TASK_STACK_SIZE, shared,
              MIDI_TASK_PRIORITY, &handle);
  return handle;
}
