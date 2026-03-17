#include "config.h"
#include "display_task.h"
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
#include <Arduino.h>
#include <cstring>

static Adafruit_PCD8544 display(PIN_SCLK, PIN_DIN, PIN_DC, PIN_CS, PIN_RST);

static void display_task_fn(void* pvParameters) {
  shared_state_t* sh = (shared_state_t*)pvParameters;
  bool last_mounted = false;
  char last_event[LAST_EVENT_LEN] = "";

  for (;;) {
    bool mounted = false;
    char event[LAST_EVENT_LEN] = "---";
    if (sh && sh->mutex) {
      if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        mounted = sh->usb_mounted;
        strncpy(event, sh->last_event, LAST_EVENT_LEN - 1);
        event[LAST_EVENT_LEN - 1] = '\0';
        xSemaphoreGive(sh->mutex);
      }
    }

    if (mounted != last_mounted || strcmp(event, last_event) != 0) {
      last_mounted = mounted;
      strncpy(last_event, event, LAST_EVENT_LEN - 1);
      last_event[LAST_EVENT_LEN - 1] = '\0';

      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(BLACK);
      display.setCursor(0, 0);
      display.println("RP2040 ZERO");
      display.setCursor(0, 10);
      display.println("Nokia 5110");
      display.setCursor(0, 20);
      display.println(mounted ? "USB MOUNTED" : "USB NOT MOUNTED");
      display.setCursor(0, 30);
      display.println(last_event);
      display.display();
    }

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
  }
}

void display_task_create(shared_state_t* shared) {
  display.begin();
  display.setContrast(55);
  display.clearDisplay();
  display.display();
  xTaskCreate(display_task_fn, "display", DISPLAY_TASK_STACK_SIZE, shared,
              DISPLAY_TASK_PRIORITY, NULL);
}
