#include "config.h"
#include "button_isr.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "FreeRTOS.h"
#include "task.h"

static TaskHandle_t s_midi_task = NULL;

static void on_button_change(void) {
  if (s_midi_task == NULL) return;
  BaseType_t woken = pdFALSE;
  uint32_t value = (gpio_get(BUTTON_PIN) == 0) ? 1 : 0; /* LOW = pressed */
  xTaskNotifyFromISR(s_midi_task, value, eSetValueWithOverwrite, &woken);
  portYIELD_FROM_ISR(woken);
}

static void gpio_callback(uint gpio, uint32_t events) {
  (void)gpio;
  (void)events;
  on_button_change();
}

void button_isr_attach(TaskHandle_t midi_task) {
  s_midi_task = midi_task;
  gpio_init(BUTTON_PIN);
  gpio_set_dir(BUTTON_PIN, GPIO_IN);
  gpio_pull_up(BUTTON_PIN);
  gpio_set_irq_enabled_with_callback(BUTTON_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, gpio_callback);
}
