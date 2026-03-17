/*
 * ui_task.h
 *
 * One task owns the NeoPixel. It only consumes events from the queue and
 * updates the LED. No MIDI, no Serial, no other peripherals.
 * RTOS reasoning: queue gives structured events; one task per peripheral.
 */

#ifndef UI_TASK_H
#define UI_TASK_H

#include <FreeRTOS.h>
#include <queue.h>

/* Call once before ui_task_create; initializes NeoPixel hardware. */
void ui_task_init(void);

/*
 * Create the UI task. ui_queue: queue of ui_event_t (LED on/off). Allocates
 * only at this call (no dynamic allocation after startup).
 */
void ui_task_create(QueueHandle_t ui_queue);

#endif /* UI_TASK_H */
