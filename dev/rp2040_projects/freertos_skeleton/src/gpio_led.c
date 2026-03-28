/*
 * Discrete blue LEDs — GPIO outputs (not onboard WS2812).
 */
#include "config.h"
#include "gpio_led.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

void gpio_led_set(uint led_gpio, bool lit) {
  gpio_put(led_gpio, lit ? LED_ACTIVE_LEVEL : LED_INACTIVE_LEVEL);
}

void gpio_led_init(void) {
  const uint pins[] = {
      LED_BLUE_SELECT_GPIO,
      LED_BLUE_MIDI_A_GPIO,
      LED_BLUE_MIDI_B_GPIO,
      LED_BLUE_MIDI_C_GPIO,
  };
  for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
    gpio_init(pins[i]);
    gpio_set_dir(pins[i], GPIO_OUT);
    gpio_put(pins[i], LED_INACTIVE_LEVEL);
  }
}
