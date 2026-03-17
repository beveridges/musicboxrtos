/*
 * button_isr.h
 *
 * Button interrupt: only notifies MIDI task. No MIDI, display, or Serial.
 * RTOS reasoning: task notification is the right primitive for a single
 * producer, single consumer, one value (press/release) — minimal latency.
 */

#ifndef BUTTON_ISR_H
#define BUTTON_ISR_H

#include <FreeRTOS.h>
#include <task.h>

/* Call once after MIDI task is created. Then call attach() to enable ISR. */
void button_isr_set_midi_task(TaskHandle_t midi_task);

/* Enable the button interrupt (CHANGE). Call after set_midi_task. */
void button_isr_attach(void);

#endif /* BUTTON_ISR_H */
