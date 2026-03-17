/*
 * config.h
 *
 * Pin assignments, MIDI constants, and RTOS sizing.
 * All allocation (queues, tasks) uses these; no dynamic allocation after startup.
 */

#ifndef CONFIG_H
#define CONFIG_H

/* Hardware: Waveshare RP2040 Zero */
#define BUTTON_PIN    12
#define NEOPIXEL_PIN  16
#define NEOPIXEL_N    1

/* MIDI: Note On C4 velocity 100 on channel 1 */
#define MIDI_CHANNEL   1
#define MIDI_NOTE_C4   60
#define MIDI_VELOCITY  100

/* Debounce: explicit delay then re-read pin (see midi_task). */
#define DEBOUNCE_MS   20

/* RTOS: one queue for MIDI->UI events; small depth. */
#define UI_QUEUE_LEN  4

/* Priorities (0 = lowest, 7 = highest on Arduino-Pico). */
#define MIDI_TASK_PRIORITY  5
#define UI_TASK_PRIORITY    2

#define MIDI_TASK_STACK_SIZE  256
#define UI_TASK_STACK_SIZE   128

#endif /* CONFIG_H */
