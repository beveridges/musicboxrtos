/*
 * Text-only menu: pot selects row, button confirms / long-press exits.
 */
#ifndef MENU_H
#define MENU_H

#include "config.h"

typedef enum {
  UI_HOME = 0,
  UI_MENU,
  UI_EDIT,
  UI_CONFIRM,
  UI_TAP_TEMPO,
} ui_state_t;

typedef enum {
  EV_NONE = 0,
  EV_ROTATE,
  EV_BUTTON_SHORT,
  EV_BUTTON_LONG,
} ui_event_type_t;

typedef struct {
  ui_event_type_t type;
  int8_t delta;
} ui_event_t;

void menu_init(shared_state_t *sh);
void menu_process_event(shared_state_t *sh, const ui_event_t *ev);
void menu_process_midi_buttons(shared_state_t *sh, uint8_t midi_btn_bits);
void menu_render(shared_state_t *sh);
/** Fills `lines` with parent menu list for hold-preview; sets *out_invert_row for list highlight. */
bool menu_parent_preview_lines(const shared_state_t *sh, char lines[MENU_ROWS][LINE_LEN], uint8_t *out_invert_row);
#endif /* MENU_H */
