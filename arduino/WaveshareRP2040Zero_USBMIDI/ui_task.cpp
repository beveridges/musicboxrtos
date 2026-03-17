/*
 * ui_task.cpp — Only place that updates NeoPixel.
 */

#include "config.h"
#include "ui_task.h"
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

static Adafruit_NeoPixel pixel(NEOPIXEL_N, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

static void ui_task_fn(void* pvParameters) {
  shared_button_t* sh = (shared_button_t*)pvParameters;
  int last = -1;

  for (;;) {
    int state = HIGH;
    if (sh != NULL && sh->mutex != NULL && sh->button_state != NULL) {
      if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        state = *sh->button_state;
        xSemaphoreGive(sh->mutex);
      }
    }
    if (state != last) {
      last = state;
      pixel.setPixelColor(0, state == LOW ? pixel.Color(255, 255, 255) : pixel.Color(0, 0, 0));
      pixel.show();
    }
    vTaskDelay(pdMS_TO_TICKS(UI_POLL_MS));
  }
}

void ui_task_create(shared_button_t* shared) {
  pixel.begin();
  pixel.setBrightness(40);
  pixel.clear();
  pixel.show();
  xTaskCreate(ui_task_fn, "ui", UI_TASK_STACK_SIZE, shared, UI_TASK_PRIORITY, NULL);
}
