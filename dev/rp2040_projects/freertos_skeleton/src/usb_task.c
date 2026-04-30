/*
 * USB device task: tud_task() + TL-gesture MSC arming + dual profile re-enumerate.
 */
#include "config.h"
#include "sb1_msc.h"
#include "usb_descriptors.h"
#include "tusb.h"
#include "device/usbd.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define USB_TASK_STACK_SIZE  (384)
#define USB_TASK_PRIORITY    (configMAX_PRIORITIES - 1)

static bool s_usb_in_msc_profile;

static void usb_host_reconnect(void) {
  (void)tud_disconnect();
  vTaskDelay(pdMS_TO_TICKS(120));
  (void)tud_connect();
}

static void usb_task_fn(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;
  sb1_msc_init();
  sb1_msc_set_shared(sh);
  usb_descriptors_set_profile_normal();
  s_usb_in_msc_profile = false;
  tusb_init();
  for (;;) {
    bool do_reconnect = false;
    bool arm_msc_from_request = false;
    if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      if (sh->usb_msc_tl_request != 0u) {
        sh->usb_msc_tl_request = 0u;
        arm_msc_from_request = true;
      } else if (s_usb_in_msc_profile && sh->usb_msc_host_ejected) {
        sh->usb_msc_host_ejected = false;
        usb_descriptors_set_profile_normal();
        s_usb_in_msc_profile = false;
        do_reconnect = true;
      }
      xSemaphoreGive(sh->mutex);
    }
    if (arm_msc_from_request) {
      /* Must run outside sh->mutex: sb1_msc_on_tl_gesture() also touches shared state. */
      sb1_msc_on_tl_gesture(sh);
      usb_descriptors_set_profile_msc_only();
      s_usb_in_msc_profile = true;
      do_reconnect = true;
    }
    if (do_reconnect) {
      usb_host_reconnect();
    }
    tud_task();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void usb_task_create(shared_state_t *sh) {
  xTaskCreate(usb_task_fn, "usb", USB_TASK_STACK_SIZE, sh, USB_TASK_PRIORITY, NULL);
}
