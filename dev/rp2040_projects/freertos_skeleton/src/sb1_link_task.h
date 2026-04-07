#ifndef SB1_LINK_TASK_H
#define SB1_LINK_TASK_H

#include "config.h"

/* Called once after stdio/UART is ready (ESP32 partner detected). */
void sb1_link_task_create(shared_state_t *shared);

#endif
