/*
 * UI task: reads shared state, drives NeoPixel (WS2812) on LED_PIN.
 * Minimal bit-bang for one pixel; idle = off, pressed = white.
 */
#include "config.h"
#include "ui_task.h"
#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "pico/time.h"

static void neopixel_put_pixel(uint32_t grb) {
  uint32_t pin = LED_PIN;
  for (int i = 23; i >= 0; i--) {
    gpio_put(pin, 1);
    if (grb & (1u << i))
      busy_wait_us(1);
    gpio_put(pin, 0);
    busy_wait_us(1);
  }
}

static void ui_task_fn(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;
  int last = -1;

  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);

  for (;;) {
    int state = 1; /* HIGH = not pressed */
    if (sh && sh->mutex) {
      if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (sh->button_state) state = *sh->button_state;
        xSemaphoreGive(sh->mutex);
      }
    }
    if (state != last) {
      last = state;
      /* 0 = pressed: white (G R B); 1 = idle: off */
      uint32_t grb = (state == 0) ? (0x00ff40u) : 0u; /* dim white: 0x40 = G */
      neopixel_put_pixel(grb);
    }
    vTaskDelay(pdMS_TO_TICKS(UI_POLL_MS));
  }
}

void ui_task_create(shared_state_t *shared) {
  xTaskCreate(ui_task_fn, "ui", UI_TASK_STACK_SIZE, shared, UI_TASK_PRIORITY, NULL);
}
