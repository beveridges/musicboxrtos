/*
 * USB device task: runs tud_task() so CDC and MIDI work.
 * Calls tusb_init() once then loops (required when not using pico_stdio_usb).
 */
#include "tusb.h"
#include "device/usbd.h"
#include "FreeRTOS.h"
#include "task.h"

#define USB_TASK_STACK_SIZE  (256)
#define USB_TASK_PRIORITY    (configMAX_PRIORITIES - 1)

static void usb_task_fn(void *pvParameters) {
  (void)pvParameters;
  tusb_init();
  for (;;) {
    tud_task();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void usb_task_create(void) {
  xTaskCreate(usb_task_fn, "usb", USB_TASK_STACK_SIZE, NULL, USB_TASK_PRIORITY, NULL);
}
