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
void menu_render(shared_state_t *sh);

#endif /* MENU_H */
