#include "config.h"
#include "ui_task.h"
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

static Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

static void ui_task_fn(void* pvParameters) {
  shared_state_t* sh = (shared_state_t*)pvParameters;
  int last = -1;

  for (;;) {
    int state = HIGH;
    if (sh && sh->mutex) {
      if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (sh->button_state) state = *sh->button_state;
        xSemaphoreGive(sh->mutex);
      }
    }
    if (state != last) {
      last = state;
      led.setPixelColor(0, state == LOW ? led.Color(255, 255, 255) : led.Color(0, 0, 0));
      led.show();
    }
    vTaskDelay(pdMS_TO_TICKS(UI_POLL_MS));
  }
}

void ui_task_create(shared_state_t* shared) {
  led.begin();
  led.setBrightness(40);
  led.clear();
  led.show();
  xTaskCreate(ui_task_fn, "ui", UI_TASK_STACK_SIZE, shared, UI_TASK_PRIORITY, NULL);
}
