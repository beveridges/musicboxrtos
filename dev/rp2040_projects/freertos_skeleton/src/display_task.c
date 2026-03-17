/*
 * Display task: reads shared state, prints USB status and last event to stdout (UART/USB serial).
 * Arduino sketch used Nokia 5110; here we use printf for same info until 5110 driver is added.
 */
#include "config.h"
#include "display_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

static void display_task_fn(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;
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
      printf("RP2040 ZERO | %s | %s\n", mounted ? "USB MOUNTED" : "USB NOT MOUNTED", last_event);
    }

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
  }
}

void display_task_create(shared_state_t *shared) {
  xTaskCreate(display_task_fn, "display", DISPLAY_TASK_STACK_SIZE, shared, DISPLAY_TASK_PRIORITY, NULL);
}
