/*
 * midi_task.h — Owns all USB MIDI library calls. Minimal interface.
 */

#ifndef MIDI_TASK_H
#define MIDI_TASK_H

#include "config.h"

/* Create MIDI task. Returns task handle for button_isr, or NULL. */
TaskHandle_t midi_task_create(shared_button_t* shared);

#endif /* MIDI_TASK_H */
