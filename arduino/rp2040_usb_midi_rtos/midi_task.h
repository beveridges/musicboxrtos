/*
 * midi_task.h
 *
 * One task owns USB MIDI (TinyUSB + FortySevenEffects MIDI). No other task
 * may call MIDI or TinyUSB device APIs.
 * RTOS reasoning: one task per peripheral avoids shared-library concurrency.
 */

#ifndef MIDI_TASK_H
#define MIDI_TASK_H

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>

/* Call once before midi_task_create; initializes USB and MIDI. */
void midi_task_init(void);

/*
 * Create the MIDI task. Allocates nothing after return (task and queue are
 * created here / in main). ui_queue: queue to send UI events to (LED on/off).
 * Returns the task handle (for button ISR). Returns NULL on failure.
 */
TaskHandle_t midi_task_create(QueueHandle_t ui_queue);

#endif /* MIDI_TASK_H */
