/*
 * button_isr.h — Capture input state and notify MIDI task only. Minimal interface.
 */

#ifndef BUTTON_ISR_H
#define BUTTON_ISR_H

#include <FreeRTOS.h>
#include <task.h>

/* Set MIDI task handle and attach interrupt. Call once after MIDI task is created. */
void button_isr_attach(TaskHandle_t midi_task);

#endif /* BUTTON_ISR_H */
