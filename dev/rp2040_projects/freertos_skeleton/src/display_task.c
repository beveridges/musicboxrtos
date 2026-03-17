/*
 * Display task: Nokia 5110 (PCD8544) — same 4-line layout as test_sketch_usb_midi_button.ino.
 */
#include "config.h"
#include "display_task.h"
#include "pcd8544.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

static void display_task_fn(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;
  char last_line4[LINE_LEN] = "";
  char last_line5[LINE_LEN] = "";

  pcd8544_init(PIN_SCLK, PIN_DIN, PIN_DC, PIN_CS, PIN_RST);
  pcd8544_set_contrast(55);

  for (;;) {
    char line4[LINE_LEN] = "---";
    char line5[LINE_LEN] = "---";
    if (sh && sh->mutex) {
      if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(line4, sh->line4, LINE_LEN - 1);
        line4[LINE_LEN - 1] = '\0';
        strncpy(line5, sh->line5, LINE_LEN - 1);
        line5[LINE_LEN - 1] = '\0';
        xSemaphoreGive(sh->mutex);
      }
    }

    if (strcmp(line4, last_line4) != 0 || strcmp(line5, last_line5) != 0) {
      strncpy(last_line4, line4, LINE_LEN - 1);
      last_line4[LINE_LEN - 1] = '\0';
      strncpy(last_line5, line5, LINE_LEN - 1);
      last_line5[LINE_LEN - 1] = '\0';

      pcd8544_clear();
      pcd8544_set_cursor(0, 0);
      pcd8544_print("RP2040 ZERO");
      pcd8544_set_cursor(0, 1);
      pcd8544_print("Nokia 5110");
      pcd8544_set_cursor(0, 2);
      pcd8544_print(line4);
      pcd8544_set_cursor(0, 3);
      pcd8544_print(line5);
      pcd8544_display();
    }

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
  }
}

void display_task_create(shared_state_t *shared) {
  xTaskCreate(display_task_fn, "display", DISPLAY_TASK_STACK_SIZE, shared, DISPLAY_TASK_PRIORITY, NULL);
}
