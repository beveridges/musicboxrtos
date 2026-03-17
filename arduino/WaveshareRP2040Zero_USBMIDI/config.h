/*
 * config.h — Pin and MIDI constants. No other project headers.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <FreeRTOS.h>
#include <semphr.h>

/* Pins (Waveshare RP2040 Zero) */
#define BUTTON_PIN    12
#define NEOPIXEL_PIN  16
#define NEOPIXEL_N    1

/* MIDI */
#define MIDI_CHANNEL   1
#define MIDI_NOTE_C4   60
#define MIDI_VELOCITY  100

/* Timing */
#define DEBOUNCE_MS    20
#define UI_POLL_MS     50

/* Task priorities (0–7 on Arduino-Pico) */
#define MIDI_TASK_PRIORITY  5
#define UI_TASK_PRIORITY    2

#define MIDI_TASK_STACK_SIZE  256
#define UI_TASK_STACK_SIZE   128

/* Shared state: MIDI task writes, UI task reads. Owned by main. */
typedef struct {
  int*              button_state;
  SemaphoreHandle_t mutex;
} shared_button_t;

#endif /* CONFIG_H */
