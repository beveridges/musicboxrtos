/*
 * ui_task.cpp
 *
 * Only this file/task touches the NeoPixel. Receives structured events
 * (LED on/off) from the queue — no shared variables, no volatile.
 */

#include "config.h"
#include "event_types.h"
#include "ui_task.h"
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

static Adafruit_NeoPixel pixel(NEOPIXEL_N, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

static void ui_task_fn(void* pvParameters) {
  QueueHandle_t queue = (QueueHandle_t)pvParameters;
  ui_event_t ev;

  for (;;) {
    if (xQueueReceive(queue, &ev, portMAX_DELAY) == pdTRUE) {
      if (ev == UI_EVENT_LED_ON) {
        pixel.setPixelColor(0, pixel.Color(255, 255, 255));
      } else {
        pixel.setPixelColor(0, pixel.Color(0, 0, 0));
      }
      pixel.show();
    }
  }
}

void ui_task_init(void) {
  pixel.begin();
  pixel.setBrightness(40);
  pixel.clear();
  pixel.show();
}

void ui_task_create(QueueHandle_t ui_queue) {
  xTaskCreate(ui_task_fn, "ui", UI_TASK_STACK_SIZE, ui_queue,
              UI_TASK_PRIORITY, NULL);
}
