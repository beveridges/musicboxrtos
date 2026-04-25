/*
 * Nokia-style vertical scroll indicator: 1px track + open-left bracket thumb (/ | \\) — no digits.
 */
#ifndef SB1_VERTICALMENU_H
#define SB1_VERTICALMENU_H

#include <stdint.h>

/**
 * Draw verticalmenu in the strip left of the wireless meter.
 * @param sel_zero_based Selected item index 0 .. total_count-1
 * @param total_count Total items on this page (entire menu for mapping)
 * @param item_first_text_row First text row index (0-based) of item list (MAIN MENU: 1)
 * @param item_text_rows Number of text rows occupied by items (MAIN MENU: 4)
 */
void sb1_verticalmenu_draw(uint8_t sel_zero_based, uint8_t total_count, uint8_t item_first_text_row,
                           uint8_t item_text_rows);

#endif
