/*
 * midi_task.cpp
 *
 * Only this file/task calls MIDI.sendNoteOn/Off and TinyUSBDevice. Debounce
 * is explicit: wait DEBOUNCE_MS then re-read pin before sending.
 */

#include "config.h"
#include "event_types.h"
#include "midi_task.h"
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <Arduino.h>

static Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

static void midi_task_fn(void* pvParameters) {
  QueueHandle_t ui_queue = (QueueHandle_t)pvParameters;
  uint32_t notif_value;

  for (;;) {
    /* Block until ISR notifies or timeout. Timeout lets us poll USB/MIDI. */
    if (xTaskNotifyWait(0, 0xFFFFFFFF, &notif_value, pdMS_TO_TICKS(10)) == pdTRUE) {
      /* Debounce: simple and explicit — wait then re-read pin. */
      vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
      int pin_state = digitalRead(BUTTON_PIN);

      if (TinyUSBDevice.mounted()) {
        if (pin_state == LOW) {
          MIDI.sendNoteOn(MIDI_NOTE_C4, MIDI_VELOCITY, MIDI_CHANNEL);
        } else {
          MIDI.sendNoteOff(MIDI_NOTE_C4, 0, MIDI_CHANNEL);
        }
      }

      /* Structured event to UI: queue is preferred over shared variable. */
      ui_event_t ev = (pin_state == LOW) ? UI_EVENT_LED_ON : UI_EVENT_LED_OFF;
      if (ui_queue != NULL) {
        xQueueSend(ui_queue, &ev, 0);
      }
    }

#ifdef TINYUSB_NEED_POLLING_TASK
    TinyUSBDevice.task();
#endif
    MIDI.read();
  }
}

void midi_task_init(void) {
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }
  usb_midi.setStringDescriptor("RP2040 Zero MIDI");
  MIDI.begin(MIDI_CHANNEL_OMNI);
}

TaskHandle_t midi_task_create(QueueHandle_t ui_queue) {
  TaskHandle_t handle = NULL;
  xTaskCreate(midi_task_fn, "midi", MIDI_TASK_STACK_SIZE, ui_queue,
              MIDI_TASK_PRIORITY, &handle);
  return handle;
}
