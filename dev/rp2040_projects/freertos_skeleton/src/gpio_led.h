#ifndef GPIO_LED_H
#define GPIO_LED_H

#include <stdbool.h>
#include <stdint.h>

void gpio_led_init(void);

/* Drive LED on (lit) or off; respects LED_ACTIVE_LEVEL in config.h. */
void gpio_led_set(uint led_gpio, bool lit);

#endif
