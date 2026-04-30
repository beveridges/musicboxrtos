/*
 * verticalmenu: 1px track at SB1_VERTMENU_TRACK_X (right edge),
 * with a simple pipe-style selected marker.
 */
#include "config.h"
#include "pcd8544.h"
#include "sb1_verticalmenu.h"

#define VM_BUMP_H 7u

static void vm_plot_px(uint8_t x, uint8_t y) {
  if (x < PCD8544_WIDTH && y < PCD8544_HEIGHT) {
    pcd8544_set_pixel(x, y, true);
  }
}

/** Selected marker: straight pipe accent beside the track. */
static void vm_draw_selected_tick(uint8_t tx, uint16_t y_top) {
  if (tx < 4u) {
    return;
  }
  const uint16_t y_bot = y_top + (uint16_t)VM_BUMP_H - 1u;
  if (y_bot >= PCD8544_HEIGHT) {
    return;
  }
  const uint8_t tick_x = (uint8_t)(tx - 3u);
  for (uint16_t y = y_top; y <= y_bot; y++) {
    vm_plot_px(tick_x, (uint8_t)y);
  }
}

void sb1_verticalmenu_draw(uint8_t sel_zero_based, uint8_t total_count, uint8_t item_first_text_row,
                           uint8_t item_text_rows)
{
  if (total_count == 0u || item_text_rows == 0u) {
    return;
  }
  if (sel_zero_based >= total_count) {
    sel_zero_based = (uint8_t)(total_count - 1u);
  }

  const uint8_t tx = SB1_VERTMENU_TRACK_X;
  const uint16_t y_track_top = (uint16_t)item_first_text_row * 8u;
  const uint16_t track_px = (uint16_t)item_text_rows * 8u;
  if (track_px < VM_BUMP_H) {
    return;
  }
  const uint16_t y_track_bot = y_track_top + track_px - 1u;

  uint16_t y_bump_top;
  if (total_count <= 1u) {
    y_bump_top = y_track_top + (track_px - VM_BUMP_H) / 2u;
  } else {
    uint16_t span = track_px - VM_BUMP_H;
    y_bump_top = y_track_top + (uint16_t)sel_zero_based * span / (uint16_t)(total_count - 1u);
  }
  if (y_bump_top + VM_BUMP_H - 1u > y_track_bot) {
    y_bump_top = y_track_bot + 1u - VM_BUMP_H;
  }

  const uint16_t y_bump_bot = y_bump_top + VM_BUMP_H - 1u;
  /* Right-side track with a blank window at selected row span. */
  for (uint16_t y = y_track_top; y <= y_track_bot && y < PCD8544_HEIGHT; y++) {
    bool in_selected_span = (y >= y_bump_top && y <= y_bump_bot);
    pcd8544_set_pixel(tx, (uint8_t)y, !in_selected_span);
  }

  vm_draw_selected_tick(tx, y_bump_top);
}
