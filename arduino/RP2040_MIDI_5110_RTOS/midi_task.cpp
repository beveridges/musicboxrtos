#include "config.h"
#include "midi_task.h"
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <Arduino.h>

static Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

static void midi_task_fn(void* pvParameters) {
  shared_state_t* sh = (shared_state_t*)pvParameters;
  uint32_t notif_value;

  for (;;) {
    if (xTaskNotifyWait(0, 0xFFFFFFFF, &notif_value, pdMS_TO_TICKS(10)) == pdTRUE) {
      vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
      int state = digitalRead(BUTTON_PIN);

      if (TinyUSBDevice.mounted()) {
        if (state == LOW) {
          MIDI.sendNoteOn(MIDI_NOTE, MIDI_VEL, MIDI_CH);
          if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            *sh->button_state = state;
            sh->usb_mounted = true;
            snprintf(sh->last_event, LAST_EVENT_LEN, "NOTE ON C4");
            xSemaphoreGive(sh->mutex);
          }
        } else {
          MIDI.sendNoteOff(MIDI_NOTE, 0, MIDI_CH);
          if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            *sh->button_state = state;
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

#ifdef TINYUSB_NEED_POLLING_TASK
    TinyUSBDevice.task();
#endif
    MIDI.read();
  }
}

TaskHandle_t midi_task_create(shared_state_t* shared) {
  static bool once;
  if (!once) {
    once = true;
    if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
    usb_midi.setStringDescriptor("RP2040 MIDI Button");
    MIDI.begin(MIDI_CHANNEL_OMNI);
  }
  TaskHandle_t handle = NULL;
  xTaskCreate(midi_task_fn, "midi", MIDI_TASK_STACK_SIZE, shared,
              MIDI_TASK_PRIORITY, &handle);
  return handle;
}
