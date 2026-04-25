#ifndef DISPLAY_TASK_H
#define DISPLAY_TASK_H

#include "config.h"

void display_task_create(shared_state_t *shared);

/** Build-metadata line for About (index 0 .. count-1). */
const char *sb1_about_get_line(unsigned idx);
unsigned sb1_about_line_count(void);

#endif
