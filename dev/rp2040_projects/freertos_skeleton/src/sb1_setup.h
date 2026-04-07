#ifndef SB1_SETUP_H
#define SB1_SETUP_H

#include <stdbool.h>

/* Called after long-hold setup gesture completes (success flash). On RP2040 this is a stub. */
void sb1_enter_setup_mode(void);

/* Controls whether UART-backed `printf` is safe to use (init/backfeed mitigation). */
void sb1_set_stdio_ready(bool ready);
bool sb1_is_stdio_ready(void);

#endif
