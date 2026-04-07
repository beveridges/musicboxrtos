/*
 * Menu FSM: text-only UI with navigation and edit modes.
 */
#include "menu.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

typedef enum {
  MENU_MAIN = 0,
  MENU_CC,
  MENU_ARP,
} menu_page_id_t;

typedef struct {
  const char *title;
  const char *items[6];
  uint8_t count;
} menu_page_t;

static const menu_page_t s_pages[] = {
    [MENU_MAIN] =
        {
            .title = "MAIN MENU",
            .items = {"PROGRAM CHG", "MIDI CC CTRL", "TAP TEMPO", "CRAZY ARP", "LIVE MODE"},
            .count = 5,
        },
    [MENU_CC] =
        {
            .title = "MIDI CC CTRL",
            .items = {"CC NUMBER", "MIDI CHAN", "BACK"},
            .count = 3,
        },
    [MENU_ARP] =
        {
            .title = "CRAZY ARP",
            .items = {"ENABLED", "RATE", "BACK"},
            .count = 3,
        },
};

static ui_state_t s_state = UI_MENU;
static menu_page_id_t s_page = MENU_MAIN;
static uint8_t s_sel = 0;
static uint8_t s_first_visible = 0;
static uint8_t s_edit_value = 0;
static uint8_t s_prev_program = 0;
static uint8_t s_prev_cc = 0;
static uint8_t s_prev_chan = 1;
static uint8_t s_prev_arp_rate = 8;
static bool s_prev_arp_enabled = false;
static uint32_t s_last_tap_ms = 0;

static int clamp_i(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void clear_menu_lines(shared_state_t *sh) {
  for (int i = 0; i < MENU_ROWS; i++) {
    sh->menu_line[i][0] = '\0';
  }
}

static void fmt_line(char *dst, const char *prefix, const char *text) {
  snprintf(dst, LINE_LEN, "%s%-*.*s", prefix, DISPLAY_COLS - (int)strlen(prefix), DISPLAY_COLS - (int)strlen(prefix), text);
  dst[DISPLAY_COLS] = '\0';
}

static void sync_visible_window(void) {
  const uint8_t visible_rows = 4; /* rows 1..4 */
  if (s_sel < s_first_visible) {
    s_first_visible = s_sel;
  } else if (s_sel >= (uint8_t)(s_first_visible + visible_rows)) {
    s_first_visible = s_sel - visible_rows + 1u;
  }
}

static void enter_edit_from_main(shared_state_t *sh) {
  s_state = UI_EDIT;
  s_prev_program = sh->program_number;
  s_edit_value = sh->program_number;
}

static void enter_edit_from_cc(shared_state_t *sh) {
  s_state = UI_EDIT;
  if (s_sel == 0) {
    s_prev_cc = sh->cc_number;
    s_edit_value = sh->cc_number;
  } else {
    s_prev_chan = sh->midi_channel;
    s_edit_value = sh->midi_channel;
  }
}

static void enter_edit_from_arp(shared_state_t *sh) {
  s_state = UI_EDIT;
  if (s_sel == 0) {
    s_prev_arp_enabled = sh->arp_enabled;
    s_edit_value = sh->arp_enabled ? 1 : 0;
  } else {
    s_prev_arp_rate = sh->arp_rate;
    s_edit_value = sh->arp_rate;
  }
}

void menu_init(shared_state_t *sh) {
  s_state = UI_MENU;
  s_page = MENU_MAIN;
  s_sel = 0;
  s_first_visible = 0;
  s_edit_value = 0;
  s_last_tap_ms = 0;
  if (sh) {
    sh->menu_active = true;
  }
}

void menu_process_event(shared_state_t *sh, const ui_event_t *ev) {
  if (!sh || !ev) return;

  /* Bluetooth pairing: exit only via LED sequence on Select (see ui_task); no menu events here. */
  if (sh->bt_pairing_active) {
    return;
  }

  /* Dashboard / live: only BL long-press re-opens menu (MIDI buttons work when inactive). */
  if (!sh->menu_active) {
    if (ev->type == EV_BUTTON_LONG) {
      menu_init(sh);
      menu_render(sh);
    }
    return;
  }

  const menu_page_t *page = &s_pages[s_page];

  if (s_state == UI_EDIT) {
    if (ev->type == EV_ROTATE && ev->delta != 0) {
      if (s_page == MENU_MAIN) {
        s_edit_value = (uint8_t)clamp_i((int)s_edit_value + (int)ev->delta, 0, 127);
      } else if (s_page == MENU_CC && s_sel == 0) {
        s_edit_value = (uint8_t)clamp_i((int)s_edit_value + (int)ev->delta, 0, 127);
      } else if (s_page == MENU_CC && s_sel == 1) {
        s_edit_value = (uint8_t)clamp_i((int)s_edit_value + (int)ev->delta, 1, 16);
      } else if (s_page == MENU_ARP && s_sel == 0) {
        s_edit_value = (uint8_t)clamp_i((int)s_edit_value + (int)ev->delta, 0, 1);
      } else if (s_page == MENU_ARP && s_sel == 1) {
        s_edit_value = (uint8_t)clamp_i((int)s_edit_value + (int)ev->delta, 1, 16);
      }
    } else if (ev->type == EV_BUTTON_SHORT) {
      if (s_page == MENU_MAIN) {
        sh->program_number = s_edit_value;
        sh->program_change_pending = true;
      } else if (s_page == MENU_CC && s_sel == 0) {
        sh->cc_number = s_edit_value;
      } else if (s_page == MENU_CC && s_sel == 1) {
        sh->midi_channel = s_edit_value;
      } else if (s_page == MENU_ARP && s_sel == 0) {
        sh->arp_enabled = (s_edit_value != 0);
      } else if (s_page == MENU_ARP && s_sel == 1) {
        sh->arp_rate = s_edit_value;
      }
      s_state = UI_MENU;
    } else if (ev->type == EV_BUTTON_LONG) {
      if (s_page == MENU_MAIN) {
        sh->program_number = s_prev_program;
      } else if (s_page == MENU_CC && s_sel == 0) {
        sh->cc_number = s_prev_cc;
      } else if (s_page == MENU_CC && s_sel == 1) {
        sh->midi_channel = s_prev_chan;
      } else if (s_page == MENU_ARP && s_sel == 0) {
        sh->arp_enabled = s_prev_arp_enabled;
      } else if (s_page == MENU_ARP && s_sel == 1) {
        sh->arp_rate = s_prev_arp_rate;
      }
      s_state = UI_MENU;
    }
    return;
  }

  if (ev->type == EV_ROTATE && ev->delta != 0) {
    s_sel = (uint8_t)clamp_i((int)s_sel + (int)ev->delta, 0, (int)page->count - 1);
    sync_visible_window();
    return;
  }

  if (ev->type == EV_BUTTON_LONG) {
    if (s_page != MENU_MAIN) {
      s_page = MENU_MAIN;
      s_sel = 0;
      s_first_visible = 0;
    }
    return;
  }

  if (ev->type != EV_BUTTON_SHORT) return;

  if (s_page == MENU_MAIN) {
    switch (s_sel) {
      case 0:
        enter_edit_from_main(sh);
        break;
      case 1:
        s_page = MENU_CC;
        s_sel = 0;
        s_first_visible = 0;
        break;
      case 2: {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (s_last_tap_ms > 0 && now > s_last_tap_ms) {
          uint32_t dt = now - s_last_tap_ms;
          if (dt >= 150 && dt <= 2000) {
            sh->tap_bpm = (uint16_t)(60000u / dt);
          }
        }
        s_last_tap_ms = now;
        snprintf(sh->line4, LINE_LEN, "TAP BPM:%u", (unsigned)sh->tap_bpm);
        snprintf(sh->line5, LINE_LEN, "TAP AGAIN");
        break;
      }
      case 3:
        s_page = MENU_ARP;
        s_sel = 0;
        s_first_visible = 0;
        break;
      case 4:
        sh->menu_active = false;
        snprintf(sh->line4, LINE_LEN, "MIDI READY");
        snprintf(sh->line5, LINE_LEN, "LIVE MODE");
        break;
      default:
        break;
    }
    return;
  }

  if (s_page == MENU_CC) {
    if (s_sel == 2) {
      s_page = MENU_MAIN;
      s_sel = 1;
      sync_visible_window();
      return;
    }
    enter_edit_from_cc(sh);
    return;
  }

  if (s_page == MENU_ARP) {
    if (s_sel == 2) {
      s_page = MENU_MAIN;
      s_sel = 3;
      sync_visible_window();
      return;
    }
    enter_edit_from_arp(sh);
  }
}

void menu_render(shared_state_t *sh) {
  if (!sh) return;

  clear_menu_lines(sh);

  const menu_page_t *page = &s_pages[s_page];
  snprintf(sh->menu_line[0], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, page->title);

  if (s_state == UI_EDIT) {
    sh->menu_invert_row = 2; /* value line: black bar, white glyphs */
    if (s_page == MENU_MAIN) {
      snprintf(sh->menu_line[1], LINE_LEN, "PROGRAM NUM");
      snprintf(sh->menu_line[2], LINE_LEN, "< %3u >", (unsigned)s_edit_value);
      snprintf(sh->menu_line[4], LINE_LEN, "SHORT=SAVE");
      snprintf(sh->menu_line[5], LINE_LEN, "LONG=CANCEL");
    } else if (s_page == MENU_CC && s_sel == 0) {
      snprintf(sh->menu_line[1], LINE_LEN, "CC NUMBER");
      snprintf(sh->menu_line[2], LINE_LEN, "< %3u >", (unsigned)s_edit_value);
      snprintf(sh->menu_line[5], LINE_LEN, "SHORT=SAVE");
    } else if (s_page == MENU_CC && s_sel == 1) {
      snprintf(sh->menu_line[1], LINE_LEN, "MIDI CHAN");
      snprintf(sh->menu_line[2], LINE_LEN, "< %2u >", (unsigned)s_edit_value);
      snprintf(sh->menu_line[5], LINE_LEN, "SHORT=SAVE");
    } else if (s_page == MENU_ARP && s_sel == 0) {
      snprintf(sh->menu_line[1], LINE_LEN, "ARP ENABLED");
      snprintf(sh->menu_line[2], LINE_LEN, "< %s >", s_edit_value ? "ON " : "OFF");
      snprintf(sh->menu_line[5], LINE_LEN, "SHORT=SAVE");
    } else if (s_page == MENU_ARP && s_sel == 1) {
      snprintf(sh->menu_line[1], LINE_LEN, "ARP RATE");
      snprintf(sh->menu_line[2], LINE_LEN, "< %2u >", (unsigned)s_edit_value);
      snprintf(sh->menu_line[5], LINE_LEN, "SHORT=SAVE");
    }
    sh->menu_sel = s_sel;
    sh->menu_dirty = true;
    return;
  }

  sync_visible_window();
  /* Highlight the visible row that matches s_sel (items on lines 1..4). */
  if (s_sel >= s_first_visible && s_sel < s_first_visible + 4u) {
    sh->menu_invert_row = (uint8_t)(1u + s_sel - s_first_visible);
  } else {
    sh->menu_invert_row = 0xFFu;
  }

  for (uint8_t row = 0; row < 4; row++) {
    uint8_t idx = s_first_visible + row;
    if (idx >= page->count) break;
    fmt_line(sh->menu_line[row + 1], idx == s_sel ? ">" : " ", page->items[idx]);
  }

  if (page->count > 4) {
    uint8_t marker = (uint8_t)((s_first_visible * 4u) / page->count);
    if (marker > 3) marker = 3;
    sh->menu_line[1 + marker][DISPLAY_COLS - 1] = '#';
  }

  if (s_page == MENU_MAIN) {
    uint8_t raw8 = (uint8_t)(sh->pot_raw >> 4);
    uint8_t filt8 = (uint8_t)(sh->pot_filtered_raw >> 4);
    snprintf(sh->menu_line[5], LINE_LEN, "R%03uF%03uS%02u", (unsigned)raw8, (unsigned)filt8, (unsigned)sh->pot_quant_step);
  } else if (s_page == MENU_CC) {
    snprintf(sh->menu_line[5], LINE_LEN, "CC:%u CH:%u", (unsigned)sh->cc_number, (unsigned)sh->midi_channel);
  } else if (s_page == MENU_ARP) {
    snprintf(sh->menu_line[5], LINE_LEN, "ARP:%s R:%u", sh->arp_enabled ? "ON" : "OFF", (unsigned)sh->arp_rate);
  }

  sh->menu_sel = s_sel;
  sh->menu_dirty = true;
}
