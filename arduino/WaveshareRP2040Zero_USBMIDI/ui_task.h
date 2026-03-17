/*
 * ui_task.h — Owns NeoPixel updates only. Minimal interface.
 */

#ifndef UI_TASK_H
#define UI_TASK_H

#include "config.h"

/* Create UI task. Reads shared button state under mutex. */
void ui_task_create(shared_button_t* shared);

#endif /* UI_TASK_H */
