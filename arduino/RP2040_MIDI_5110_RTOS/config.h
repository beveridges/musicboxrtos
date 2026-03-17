/*
 * config.h — Pins and constants. Nokia 5110, NeoPixel, button, USB MIDI.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <FreeRTOS.h>
#include <semphr.h>

/* Nokia 5110 (PCD8544) */
#define PIN_SCLK  10
#define PIN_DIN   11
#define PIN_DC    14
#define PIN_CS    13
#define PIN_RST   15

/* NeoPixel */
#define LED_PIN    16
#define LED_COUNT  1

/* Button */
#define BUTTON_PIN 12

/* MIDI */
#define MIDI_CH    1
#define MIDI_NOTE  60
#define MIDI_VEL   100

/* Timing */
#define DEBOUNCE_MS    20
#define UI_POLL_MS     50
#define DISPLAY_POLL_MS 200

/* Task priorities (0–7) */
#define MIDI_TASK_PRIORITY    5
#define UI_TASK_PRIORITY      2
#define DISPLAY_TASK_PRIORITY 1

#define MIDI_TASK_STACK_SIZE    256
#define UI_TASK_STACK_SIZE      128
#define DISPLAY_TASK_STACK_SIZE 256

#define LAST_EVENT_LEN 24

/* Shared state: MIDI task writes; UI and display tasks read under mutex. */
typedef struct {
  int*              button_state;
  bool              usb_mounted;
  char              last_event[LAST_EVENT_LEN];
  SemaphoreHandle_t mutex;
} shared_state_t;

#endif /* CONFIG_H */
