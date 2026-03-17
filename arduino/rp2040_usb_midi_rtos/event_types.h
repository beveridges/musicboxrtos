/*
 * event_types.h
 *
 * Event and notification value definitions for inter-task communication.
 * RTOS reasoning:
 *   - Task notifications carry a single value (button state); no struct needed.
 *   - Queue carries structured UI events (LED on/off) so consumer gets clear semantics.
 */

#ifndef EVENT_TYPES_H
#define EVENT_TYPES_H

#include <stdint.h>

/* Values sent via xTaskNotifyFromISR to MIDI task (single fast signal). */
#define BUTTON_PRESSED  1
#define BUTTON_RELEASED 0

/* Queue item type: structured event for UI task (one byte is enough). */
typedef uint8_t ui_event_t;

#define UI_EVENT_LED_OFF 0
#define UI_EVENT_LED_ON  1

#endif /* EVENT_TYPES_H */
