/*
 * verticalmenu: 1px track at SB1_VERTMENU_TRACK_X (right edge), stylized 7-row thumb:
 * | | / | \ | | (left/right kinks around a center spine), no digits.
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

/** Stylized thumb: | | / | \ | | with center spine at tx-2. */
static void vm_draw_bracket_thumb(uint8_t tx, uint16_t y_top) {
  if (tx < 4u) {
    return;
  }
  const uint16_t y_bot = y_top + (uint16_t)VM_BUMP_H - 1u;
  if (y_bot >= PCD8544_HEIGHT) {
    return;
  }
  const uint8_t spine_x = (uint8_t)(tx - 2u);

  vm_plot_px(spine_x, (uint8_t)(y_top + 0u)); /* | */
  vm_plot_px(spine_x, (uint8_t)(y_top + 1u)); /* | */
  vm_plot_px((uint8_t)(spine_x - 1u), (uint8_t)(y_top + 2u)); /* / */
  vm_plot_px(spine_x, (uint8_t)(y_top + 3u)); /* | */
  vm_plot_px((uint8_t)(spine_x + 1u), (uint8_t)(y_top + 4u)); /* \ */
  vm_plot_px(spine_x, (uint8_t)(y_top + 5u)); /* | */
  vm_plot_px(spine_x, (uint8_t)(y_top + 6u)); /* | */
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

  /* Full-height vertical track */
  for (uint16_t y = y_track_top; y <= y_track_bot && y < PCD8544_HEIGHT; y++) {
    pcd8544_set_pixel(tx, (uint8_t)y, true);
  }

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

  vm_draw_bracket_thumb(tx, y_bump_top);
}
