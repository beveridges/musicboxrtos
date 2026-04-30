/*
 * TinyUSB MSC RAM disk (SB1STORAGE) + root .MID listing for menu.
 */
#ifndef SB1_MSC_H
#define SB1_MSC_H

#include "config.h"
#include <stdint.h>

void sb1_msc_init(void);
void sb1_msc_set_shared(shared_state_t *sh);
void sb1_msc_on_tl_gesture(shared_state_t *sh);
void sb1_msc_refresh_file_list(shared_state_t *sh);

#endif
