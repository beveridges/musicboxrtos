/*
 * Menu FSM: root INSTRUMENT / HUB / UTILITY / SYSTEM + legacy performance submenus.
 */
#include "menu.h"
#include "display_task.h"
#include "pico/time.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

typedef enum {
  PG_ROOT = 0,
  PG_INSTRUMENT,
  PG_CC,
  PG_ARP,
  PG_HUB,
  PG_HUB_MIDI,
  PG_HUB_OSC,
  PG_UTILITY,
  PG_UTILITY_FILES,
  PG_UTILITY_DIAG,
  PG_SETTINGS,
  PG_SETTINGS_AUTO,
  PG_SETTINGS_FW,
  PG_SETTINGS_FW_OTA,
  PG_SETTINGS_DEVELOPER,
  PG_SETTINGS_ABOUT,
  PG__COUNT
} menu_page_id_t;

typedef struct {
  const char *title;
  const char *items[6];
  uint8_t count;
} menu_page_t;

static const menu_page_t s_pages[PG__COUNT] = {
    [PG_ROOT] =
        {
            .title = "MAIN MENU",
            .items = {"INSTRUMENT", "HUB", "UTILITY", "SYSTEM"},
            .count = 4,
        },
    [PG_INSTRUMENT] =
        {
            .title = "INSTRUMENT",
            .items = {"PROGRAM CHG", "MIDI CC CTRL", "TAP TEMPO", "CRAZY ARP", "LIVE MODE"},
            .count = 5,
        },
    [PG_CC] =
        {
            .title = "MIDI CC CTRL",
            .items = {"CC NUMBER", "MIDI CHAN", "BACK"},
            .count = 3,
        },
    [PG_ARP] =
        {
            .title = "CRAZY ARP",
            .items = {"ENABLED", "RATE", "BACK"},
            .count = 3,
        },
    [PG_HUB] =
        {
            .title = "HUB",
            .items = {"MIDI", "OSC"},
            .count = 2,
        },
    [PG_HUB_MIDI] =
        {
            .title = "HUB MIDI",
            .items = {"BLE MIDI IN", "BACK"},
            .count = 2,
        },
    [PG_HUB_OSC] =
        {
            .title = "OSC",
            .items = {"BACK"},
            .count = 1,
        },
    [PG_UTILITY] =
        {
            .title = "UTILITY",
            .items = {"MIDI FILES", "DIAGNOSTICS", "BACK"},
            .count = 3,
        },
    [PG_UTILITY_FILES] =
        {
            .title = "MIDI FILES",
            .items = {NULL},
            .count = 0,
        },
    [PG_UTILITY_DIAG] =
        {
            .title = "DIAGNOSTICS",
            .items = {"BACK"},
            .count = 1,
        },
    [PG_SETTINGS] =
        {
            .title = "SYSTEM",
            .items = {"AUTO OFF", "FIRMWARE", "DEVELOPER", "ABOUT"},
            .count = 4,
        },
    [PG_SETTINGS_AUTO] =
        {
            .title = "AUTO OFF",
            .items = {"BACK"},
            .count = 1,
        },
    [PG_SETTINGS_FW] =
        {
            .title = "FIRMWARE",
            .items = {"BACK"},
            .count = 1,
        },
    [PG_SETTINGS_FW_OTA] =
        {
            .title = "UPDATE",
            .items = {"BACK"},
            .count = 1,
        },
    [PG_SETTINGS_DEVELOPER] =
        {
            .title = "DEVELOPER",
            .items = {"BACK"},
            .count = 1,
        },
    [PG_SETTINGS_ABOUT] =
        {
            .title = "ABOUT",
            .items = {"BACK"},
            .count = 1,
        },
};

static ui_state_t s_state = UI_MENU;
static menu_page_id_t s_page = PG_ROOT;
static uint8_t s_sel = 0;
static uint8_t s_first_visible = 0;
static uint8_t s_edit_value = 0;
static uint8_t s_prev_program = 0;
static uint8_t s_prev_cc = 0;
static uint8_t s_prev_chan = 1;
static uint8_t s_prev_arp_rate = 8;
static bool s_prev_arp_enabled = false;
static uint8_t s_prev_hub_midi_sink = SB1_BLE_MIDI_SINK_MERGE;
static uint32_t s_last_tap_ms = 0;
static uint8_t s_prev_auto_shutdown_idx = 0;
static bool s_tap_br_was_down = false;

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

static uint8_t menu_item_visible_rows(menu_page_id_t page_id) {
  switch (page_id) {
    case PG_ROOT:
      return 4u;
    case PG_INSTRUMENT:
      return 4u;
    case PG_SETTINGS:
      return 4u;
    case PG_UTILITY_FILES:
      return 0u;
    default:
      return 1u;
  }
}

static void sync_visible_window(void) {
  const uint8_t visible_rows = menu_item_visible_rows(s_page);
  if (s_sel < s_first_visible) {
    s_first_visible = s_sel;
  } else if (s_sel >= (uint8_t)(s_first_visible + visible_rows)) {
    s_first_visible = s_sel - visible_rows + 1u;
  }
}

static void menu_nav_back(shared_state_t *sh) {
  if (sh && s_page == PG_SETTINGS_ABOUT && sh->about_detail_open) {
    sh->about_detail_open = false;
    sh->about_detail_text[0] = '\0';
    sh->menu_dirty = true;
    return;
  }
  switch (s_page) {
    case PG_CC:
      s_page = PG_INSTRUMENT;
      s_sel = 1;
      break;
    case PG_ARP:
      s_page = PG_INSTRUMENT;
      s_sel = 3;
      break;
    case PG_INSTRUMENT:
      s_page = PG_ROOT;
      s_sel = 0;
      break;
    case PG_HUB_MIDI:
      s_page = PG_HUB;
      s_sel = 0;
      break;
    case PG_HUB_OSC:
      s_page = PG_HUB;
      s_sel = 1;
      break;
    case PG_HUB:
      s_page = PG_ROOT;
      s_sel = 1;
      break;
    case PG_UTILITY_FILES:
      s_page = PG_UTILITY;
      s_sel = 0;
      break;
    case PG_UTILITY_DIAG:
      s_page = PG_UTILITY;
      s_sel = 1;
      break;
    case PG_UTILITY:
      s_page = PG_ROOT;
      s_sel = 2;
      break;
    case PG_SETTINGS_AUTO:
      s_page = PG_SETTINGS;
      s_sel = 0;
      break;
    case PG_SETTINGS_FW_OTA:
      s_page = PG_SETTINGS_FW;
      s_sel = 0;
      break;
    case PG_SETTINGS_DEVELOPER:
      s_page = PG_SETTINGS;
      s_sel = 2;
      break;
    case PG_SETTINGS_ABOUT:
      s_page = PG_SETTINGS;
      s_sel = 3;
      break;
    case PG_SETTINGS:
      s_page = PG_ROOT;
      s_sel = 3;
      break;
    case PG_SETTINGS_FW:
      if (sh && sh->fw_ota_step != 0) {
        sh->fw_ota_step = 0;
      } else {
        s_page = PG_SETTINGS;
        s_sel = 1;
      }
      break;
    default:
      break;
  }
  s_first_visible = 0;
  sync_visible_window();
}

static bool menu_nav_back_peek(menu_page_id_t cur, menu_page_id_t *parent, uint8_t *parent_sel) {
  switch (cur) {
    case PG_CC:
      *parent = PG_INSTRUMENT;
      *parent_sel = 1;
      return true;
    case PG_ARP:
      *parent = PG_INSTRUMENT;
      *parent_sel = 3;
      return true;
    case PG_INSTRUMENT:
      *parent = PG_ROOT;
      *parent_sel = 0;
      return true;
    case PG_HUB_MIDI:
      *parent = PG_HUB;
      *parent_sel = 0;
      return true;
    case PG_HUB_OSC:
      *parent = PG_HUB;
      *parent_sel = 1;
      return true;
    case PG_HUB:
      *parent = PG_ROOT;
      *parent_sel = 1;
      return true;
    case PG_UTILITY_FILES:
      *parent = PG_UTILITY;
      *parent_sel = 0;
      return true;
    case PG_UTILITY_DIAG:
      *parent = PG_UTILITY;
      *parent_sel = 1;
      return true;
    case PG_UTILITY:
      *parent = PG_ROOT;
      *parent_sel = 2;
      return true;
    case PG_SETTINGS_AUTO:
      *parent = PG_SETTINGS;
      *parent_sel = 0;
      return true;
    case PG_SETTINGS_FW_OTA:
      *parent = PG_SETTINGS_FW;
      *parent_sel = 0;
      return true;
    case PG_SETTINGS_FW:
      *parent = PG_SETTINGS;
      *parent_sel = 1;
      return true;
    case PG_SETTINGS_DEVELOPER:
      *parent = PG_SETTINGS;
      *parent_sel = 2;
      return true;
    case PG_SETTINGS_ABOUT:
      *parent = PG_SETTINGS;
      *parent_sel = 3;
      return true;
    case PG_SETTINGS:
      *parent = PG_ROOT;
      *parent_sel = 3;
      return true;
    default:
      return false;
  }
}

static uint8_t menu_first_visible_for_sel(uint8_t sel, uint8_t page_count, menu_page_id_t page_id) {
  (void)page_count;
  uint8_t fv = 0;
  const uint8_t visible_rows = menu_item_visible_rows(page_id);
  if (sel < fv) {
    return sel;
  }
  if (sel >= (uint8_t)(fv + visible_rows)) {
    return sel - visible_rows + 1u;
  }
  return 0;
}

static void menu_render_list_page_into(char lines[MENU_ROWS][LINE_LEN], const shared_state_t *sh,
                                      menu_page_id_t page_id, uint8_t sel, uint8_t *out_invert_row) {
  for (int i = 0; i < MENU_ROWS; i++) {
    lines[i][0] = '\0';
  }
  const menu_page_t *page = &s_pages[page_id];
  snprintf(lines[0], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, page->title);

  uint8_t first_visible = menu_first_visible_for_sel(sel, page->count, page_id);
  /* Row XOR inverse (pcd8544_invert_row) turns every untouched framebuffer 0 into solid ink;
   * proportional font + short lines leave large gaps → black bars/borders. Highlight with > only. */
  *out_invert_row = 0xFFu;

  const uint8_t item_rows = menu_item_visible_rows(page_id);
  for (uint8_t row = 0; row < item_rows; row++) {
    uint8_t idx = (uint8_t)(first_visible + row);
    if (idx >= page->count) {
      lines[row + 1][0] = '\0';
      continue;
    }
    fmt_line(lines[row + 1], idx == sel ? ">" : " ", page->items[idx]);
  }

  if (page_id == PG_HUB_OSC) {
    snprintf(lines[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "NOT IMPLEMENTED");
    snprintf(lines[3], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "PLANNED BRIDGE");
    *out_invert_row = 0xFFu;
  } else if (page_id == PG_UTILITY_FILES) {
    snprintf(lines[1], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "STORAGE: TODO");
    snprintf(lines[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "VOL:SB1STORAGE");
    snprintf(lines[3], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "DRAG .MID FILES");
    *out_invert_row = 0xFFu;
  } else if (page_id == PG_UTILITY_DIAG) {
    snprintf(lines[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "COMING SOON");
    *out_invert_row = 0xFFu;
  } else if (page_id == PG_SETTINGS_DEVELOPER) {
    snprintf(lines[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "COMING SOON");
    *out_invert_row = 0xFFu;
  }

  if (page_id == PG_CC) {
    snprintf(lines[2], LINE_LEN, "CC:%u CH:%u", (unsigned)sh->cc_number, (unsigned)sh->midi_channel);
  } else if (page_id == PG_ARP) {
    snprintf(lines[2], LINE_LEN, "ARP:%s R:%u", sh->arp_enabled ? "ON" : "OFF", (unsigned)sh->arp_rate);
  } else if (page_id == PG_HUB_MIDI) {
    snprintf(lines[2], LINE_LEN, "IN:%s", sh->ble_midi_sink == SB1_BLE_MIDI_SINK_USB
                                                ? "USB"
                                                : (sh->ble_midi_sink == SB1_BLE_MIDI_SINK_MERGE ? "MRG" : "DEV"));
  }

  if (page_id != PG_ROOT) {
    snprintf(lines[MENU_ROWS - 1], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, " LONG=BACK ");
  }
}

bool menu_parent_preview_lines(const shared_state_t *sh, char lines[MENU_ROWS][LINE_LEN], uint8_t *out_invert_row) {
  if (!sh || !lines || !out_invert_row) {
    return false;
  }
  if (s_state != UI_MENU) {
    return false;
  }
  menu_page_id_t parent;
  uint8_t psel;
  if (!menu_nav_back_peek(s_page, &parent, &psel)) {
    return false;
  }
  menu_render_list_page_into(lines, sh, parent, psel, out_invert_row);
  return true;
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

static void enter_edit_from_hub_midi(shared_state_t *sh) {
  s_state = UI_EDIT;
  s_prev_hub_midi_sink = sh->ble_midi_sink;
  if (s_prev_hub_midi_sink > SB1_BLE_MIDI_SINK_DEVICE) {
    s_prev_hub_midi_sink = SB1_BLE_MIDI_SINK_MERGE;
  }
  s_edit_value = s_prev_hub_midi_sink;
}

static uint8_t auto_shutdown_minutes_to_idx(uint8_t minutes) {
  switch (minutes) {
    case 0:
      return 0;
    case 5:
      return 1;
    case 15:
      return 2;
    case 30:
      return 3;
    case 60:
      return 4;
    case 120:
      return 5;
    default:
      return 0;
  }
}

static uint8_t auto_shutdown_idx_to_minutes(uint8_t idx) {
  static const uint8_t mins[] = {0, 5, 15, 30, 60, 120};
  if (idx >= 6) {
    return 0;
  }
  return mins[idx];
}

static void enter_edit_settings_auto(shared_state_t *sh) {
  s_state = UI_EDIT;
  s_page = PG_SETTINGS_AUTO;
  s_sel = 0;
  s_prev_auto_shutdown_idx = auto_shutdown_minutes_to_idx(sh->auto_shutdown_minutes);
  s_edit_value = s_prev_auto_shutdown_idx;
}

void menu_init(shared_state_t *sh) {
  s_state = UI_MENU;
  s_page = PG_ROOT;
  s_sel = 0;
  s_first_visible = 0;
  s_edit_value = 0;
  s_last_tap_ms = 0;
  s_tap_br_was_down = false;
  if (sh) {
    sh->menu_active = true;
    sh->live_mode_active = false;
    sh->menu_view = SB1_MENU_VIEW_LIST;
    sh->menu_parent_preview = false;
    sh->menu_verticalmenu_enable = false;
    sh->menu_vm_count = 0;
    sh->fw_ota_step = 0;
    sh->about_line_sel = 0;
    sh->about_detail_open = false;
    sh->about_detail_text[0] = '\0';
  }
}

void menu_process_event(shared_state_t *sh, const ui_event_t *ev) {
  if (!sh || !ev) return;

  if (sh->bt_pairing_active) {
    return;
  }

  if (!sh->menu_active) {
    if (ev->type == EV_BUTTON_LONG) {
      menu_init(sh);
      menu_render(sh);
    }
    return;
  }

  const menu_page_t *page = &s_pages[s_page];

  /* Tap tempo: BPM is BR (MIDI A) tap only; EV_ROTATE (pot navigation) is ignored (short/long BL only). */
  if (s_state == UI_TAP_TEMPO) {
    if (ev->type == EV_BUTTON_SHORT) {
      s_state = UI_MENU;
      s_last_tap_ms = 0;
      sh->menu_dirty = true;
    } else if (ev->type == EV_BUTTON_LONG) {
      s_state = UI_MENU;
      s_last_tap_ms = 0;
      menu_nav_back(sh);
      sh->menu_dirty = true;
    }
    return;
  }

  if (s_state == UI_EDIT) {
    if (ev->type == EV_ROTATE && ev->delta != 0) {
      if (s_page == PG_INSTRUMENT) {
        s_edit_value = (uint8_t)clamp_i((int)s_edit_value + (int)ev->delta, 0, 127);
      } else if (s_page == PG_CC && s_sel == 0) {
        s_edit_value = (uint8_t)clamp_i((int)s_edit_value + (int)ev->delta, 0, 127);
      } else if (s_page == PG_CC && s_sel == 1) {
        s_edit_value = (uint8_t)clamp_i((int)s_edit_value + (int)ev->delta, 1, 16);
      } else if (s_page == PG_ARP && s_sel == 0) {
        s_edit_value = (uint8_t)clamp_i((int)s_edit_value + (int)ev->delta, 0, 1);
      } else if (s_page == PG_ARP && s_sel == 1) {
        s_edit_value = (uint8_t)clamp_i((int)s_edit_value + (int)ev->delta, 1, 16);
      } else if (s_page == PG_HUB_MIDI && s_sel == 0) {
        s_edit_value = (uint8_t)clamp_i((int)s_edit_value + (int)ev->delta, 0, 2);
      } else if (s_page == PG_SETTINGS_AUTO) {
        s_edit_value = (uint8_t)clamp_i((int)s_edit_value + (int)ev->delta, 0, 5);
      }
    } else if (ev->type == EV_BUTTON_SHORT) {
      if (s_page == PG_INSTRUMENT) {
        sh->program_number = s_edit_value;
        sh->program_change_pending = true;
      } else if (s_page == PG_CC && s_sel == 0) {
        sh->cc_number = s_edit_value;
      } else if (s_page == PG_CC && s_sel == 1) {
        sh->midi_channel = s_edit_value;
      } else if (s_page == PG_ARP && s_sel == 0) {
        sh->arp_enabled = (s_edit_value != 0);
      } else if (s_page == PG_ARP && s_sel == 1) {
        sh->arp_rate = s_edit_value;
      } else if (s_page == PG_HUB_MIDI && s_sel == 0) {
        sh->ble_midi_sink = s_edit_value;
      } else if (s_page == PG_SETTINGS_AUTO) {
        sh->auto_shutdown_minutes = auto_shutdown_idx_to_minutes(s_edit_value);
        s_page = PG_SETTINGS;
        s_sel = 0;
      }
      s_state = UI_MENU;
    } else if (ev->type == EV_BUTTON_LONG) {
      if (s_page == PG_INSTRUMENT) {
        sh->program_number = s_prev_program;
      } else if (s_page == PG_CC && s_sel == 0) {
        sh->cc_number = s_prev_cc;
      } else if (s_page == PG_CC && s_sel == 1) {
        sh->midi_channel = s_prev_chan;
      } else if (s_page == PG_ARP && s_sel == 0) {
        sh->arp_enabled = s_prev_arp_enabled;
      } else if (s_page == PG_ARP && s_sel == 1) {
        sh->arp_rate = s_prev_arp_rate;
      } else if (s_page == PG_HUB_MIDI && s_sel == 0) {
        sh->ble_midi_sink = s_prev_hub_midi_sink;
      } else if (s_page == PG_SETTINGS_AUTO) {
        sh->auto_shutdown_minutes = auto_shutdown_idx_to_minutes(s_prev_auto_shutdown_idx);
        s_page = PG_SETTINGS;
        s_sel = 0;
      }
      s_state = UI_MENU;
    }
    return;
  }

  if (ev->type == EV_ROTATE && ev->delta != 0) {
    if (s_page == PG_SETTINGS_ABOUT) {
      if (!sh->about_detail_open) {
        int n = (int)sb1_about_line_count();
        if (n > 0) {
          sh->about_line_sel =
              (uint8_t)clamp_i((int)sh->about_line_sel + (int)ev->delta, 0, n - 1);
        }
        sh->menu_dirty = true;
      }
      return;
    }
    if (s_page == PG_SETTINGS_FW) {
      if (sh->bt_peer_connected || sh->wifi_sta_connected) {
        s_sel = (uint8_t)clamp_i((int)s_sel + (int)ev->delta, 0, 1);
      }
      sh->menu_dirty = true;
      return;
    }
    if (page->count == 0u) {
      return;
    }
    s_sel = (uint8_t)clamp_i((int)s_sel + (int)ev->delta, 0, (int)page->count - 1);
    sync_visible_window();
    return;
  }

  if (ev->type == EV_BUTTON_LONG) {
    if (s_page != PG_ROOT) {
      menu_nav_back(sh);
    }
    return;
  }

  if (ev->type != EV_BUTTON_SHORT) return;

  /* Short press: navigation and actions */
  if (s_page == PG_ROOT) {
    switch (s_sel) {
      case 0:
        s_page = PG_INSTRUMENT;
        s_sel = 0;
        break;
      case 1:
        s_page = PG_HUB;
        s_sel = 0;
        break;
      case 2:
        s_page = PG_UTILITY;
        s_sel = 0;
        break;
      case 3:
        s_page = PG_SETTINGS;
        s_sel = 0;
        break;
      default:
        break;
    }
    s_first_visible = 0;
    return;
  }

  if (s_page == PG_INSTRUMENT) {
    switch (s_sel) {
      case 0:
        enter_edit_from_main(sh);
        break;
      case 1:
        s_page = PG_CC;
        s_sel = 0;
        s_first_visible = 0;
        break;
      case 2:
        s_state = UI_TAP_TEMPO;
        s_last_tap_ms = 0;
        s_tap_br_was_down = false;
        sh->menu_dirty = true;
        break;
      case 3:
        s_page = PG_ARP;
        s_sel = 0;
        s_first_visible = 0;
        break;
      case 4:
        /* LIVE MODE: pot_task / LCD; menu list ignores knob (ui_task EV_ROTATE gate). */
        sh->menu_active = false;
        sh->live_mode_active = true;
        break;
      default:
        break;
    }
    return;
  }

  if (s_page == PG_CC) {
    if (s_sel == 2) {
      s_page = PG_INSTRUMENT;
      s_sel = 1;
      sync_visible_window();
      return;
    }
    enter_edit_from_cc(sh);
    return;
  }

  if (s_page == PG_ARP) {
    if (s_sel == 2) {
      s_page = PG_INSTRUMENT;
      s_sel = 3;
      sync_visible_window();
      return;
    }
    enter_edit_from_arp(sh);
    return;
  }

  if (s_page == PG_HUB) {
    if (s_sel == 0) {
      s_page = PG_HUB_MIDI;
      s_sel = 0;
    } else {
      s_page = PG_HUB_OSC;
      s_sel = 0;
    }
    s_first_visible = 0;
    return;
  }

  if (s_page == PG_HUB_MIDI) {
    if (s_sel == 1) {
      s_page = PG_HUB;
      s_sel = 0;
    } else {
      enter_edit_from_hub_midi(sh);
    }
    return;
  }

  if (s_page == PG_HUB_OSC) {
    s_page = PG_HUB;
    s_sel = 1;
    s_first_visible = 0;
    return;
  }

  if (s_page == PG_UTILITY) {
    if (s_sel == 0) {
      s_page = PG_UTILITY_FILES;
      s_sel = 0;
    } else if (s_sel == 1) {
      s_page = PG_UTILITY_DIAG;
      s_sel = 0;
    } else {
      s_page = PG_ROOT;
      s_sel = 2;
    }
    s_first_visible = 0;
    return;
  }

  if (s_page == PG_UTILITY_DIAG) {
    if (s_sel != 0) {
      return;
    }
    s_page = PG_UTILITY;
    s_sel = 1;
    s_first_visible = 0;
    return;
  }

  if (s_page == PG_SETTINGS) {
    if (s_sel == 0) {
      enter_edit_settings_auto(sh);
    } else if (s_sel == 1) {
      s_page = PG_SETTINGS_FW;
      s_sel = 0;
      sh->fw_ota_step = 0;
    } else if (s_sel == 2) {
      s_page = PG_SETTINGS_DEVELOPER;
      s_sel = 0;
    } else if (s_sel == 3) {
      s_page = PG_SETTINGS_ABOUT;
      s_sel = 0;
      sh->about_line_sel = 0;
      sh->about_detail_open = false;
      sh->about_detail_text[0] = '\0';
    }
    s_first_visible = 0;
    return;
  }

  if (s_page == PG_SETTINGS_FW) {
    bool links = sh->bt_peer_connected || sh->wifi_sta_connected;
    if (sh->fw_ota_step == 1u) {
      bool yes = (s_sel == 0u);
      sh->fw_ota_step = 0;
      if (yes) {
        /* Future: start OTA download here (stub returns to main FIRMWARE screen). */
      }
      menu_render(sh);
      s_first_visible = 0;
      return;
    }
    if (links) {
      if (s_sel == 0u) {
        sh->fw_ota_step = 1u;
      } else {
        s_page = PG_SETTINGS;
        s_sel = 1;
      }
      menu_render(sh);
      s_first_visible = 0;
      return;
    }
    if (!links) {
      s_page = PG_SETTINGS;
      s_sel = 1;
    }
    s_first_visible = 0;
    menu_render(sh);
    return;
  }

  if (s_page == PG_SETTINGS_FW_OTA) {
    return;
  }

  if (s_page == PG_SETTINGS_DEVELOPER) {
    if (s_sel != 0) {
      return;
    }
    s_page = PG_SETTINGS;
    s_sel = 2;
    s_first_visible = 0;
    return;
  }

  if (s_page == PG_SETTINGS_ABOUT) {
    if (sh->about_detail_open) {
      sh->about_detail_open = false;
      sh->about_detail_text[0] = '\0';
      sh->menu_dirty = true;
      return;
    }
    {
      const char *ln = sb1_about_get_line((unsigned)sh->about_line_sel);
      if (ln) {
        strncpy(sh->about_detail_text, ln, SB1_ABOUT_DETAIL_BUF - 1u);
        sh->about_detail_text[SB1_ABOUT_DETAIL_BUF - 1u] = '\0';
        sh->about_detail_open = true;
        sh->menu_dirty = true;
      }
    }
    return;
  }
}

void menu_process_midi_buttons(shared_state_t *sh, uint8_t midi_btn_bits) {
  if (!sh) {
    return;
  }

  bool br_down = (midi_btn_bits & 1u) != 0u;
  if (!(sh->menu_active && s_state == UI_TAP_TEMPO && s_page == PG_INSTRUMENT && s_sel == 2u)) {
    s_tap_br_was_down = br_down;
    return;
  }

  if (br_down && !s_tap_br_was_down) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (s_last_tap_ms > 0u && now > s_last_tap_ms) {
      uint32_t dt = now - s_last_tap_ms;
      if (dt >= 150u && dt <= 2000u) {
        sh->tap_bpm = (uint16_t)((60000u + dt / 2u) / dt);
      }
    }
    s_last_tap_ms = now;
    sh->menu_dirty = true;
  }

  s_tap_br_was_down = br_down;
}

void menu_render(shared_state_t *sh) {
  if (!sh) return;

  sh->menu_verticalmenu_enable = false;
  sh->menu_vm_count = 0;

  sh->menu_esc_available = ((s_state == UI_MENU || s_state == UI_TAP_TEMPO) && s_page != PG_ROOT);

  if (s_state == UI_MENU && s_page == PG_SETTINGS_FW_OTA) {
    clear_menu_lines(sh);
    sh->menu_view = SB1_MENU_VIEW_SYSTEM_FW_OTA;
    sh->menu_invert_row = 0xFFu;
    sh->menu_sel = s_sel;
    sh->menu_dirty = true;
    return;
  }

  if (s_state == UI_TAP_TEMPO && s_page == PG_INSTRUMENT && s_sel == 2u) {
    clear_menu_lines(sh);
    sh->menu_view = SB1_MENU_VIEW_TAP_TEMPO;
    sh->menu_invert_row = 0xFFu;
    sh->menu_sel = s_sel;
    sh->menu_dirty = true;
    return;
  }

  if (s_state == UI_MENU && s_page == PG_SETTINGS_ABOUT) {
    clear_menu_lines(sh);
    sh->menu_view =
        sh->about_detail_open ? SB1_MENU_VIEW_SYSTEM_ABOUT_DETAIL : SB1_MENU_VIEW_SYSTEM_ABOUT;
    sh->menu_invert_row = 0xFFu;
    sh->menu_sel = s_sel;
    if (sh->about_detail_open) {
      sh->menu_verticalmenu_enable = false;
      sh->menu_vm_count = 0;
    } else {
      sh->menu_verticalmenu_enable = true;
      sh->menu_vm_count = (uint8_t)SB1_ABOUT_INFO_LINE_COUNT;
    }
    sh->menu_dirty = true;
    return;
  }

  if (s_state == UI_MENU && s_page == PG_SETTINGS_FW && sh->fw_ota_step == 1u) {
    clear_menu_lines(sh);
    sh->menu_view = SB1_MENU_VIEW_SYSTEM_FW_SURE;
    sh->menu_invert_row = 0xFFu;
    sh->menu_sel = s_sel;
    sh->menu_dirty = true;
    return;
  }

  if (s_state == UI_MENU && s_page == PG_SETTINGS_FW) {
    clear_menu_lines(sh);
    snprintf(sh->menu_line[0], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, s_pages[PG_SETTINGS_FW].title);
    snprintf(sh->menu_line[1], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "");
    if (sh->bt_peer_connected || sh->wifi_sta_connected) {
      snprintf(sh->menu_line[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, " UPDATE?? ");
      snprintf(sh->menu_line[3], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS,
               (s_sel == 0u) ? ">YES | NO " : " YES | >NO ");
    } else {
      snprintf(sh->menu_line[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "");
      snprintf(sh->menu_line[3], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "");
    }
    if (sh->menu_esc_available) {
      snprintf(sh->menu_line[MENU_ROWS - 1], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, " LONG=BACK ");
    }
    sh->menu_view = SB1_MENU_VIEW_SYSTEM_FW;
    sh->menu_invert_row = 0xFFu;
    sh->menu_sel = s_sel;
    sh->menu_dirty = true;
    return;
  }

  clear_menu_lines(sh);

  const menu_page_t *page = &s_pages[s_page];
  snprintf(sh->menu_line[0], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, page->title);

  if (s_state == UI_EDIT) {
    sh->menu_view = SB1_MENU_VIEW_LIST;
    sh->menu_invert_row = 0xFFu;
    if (s_page == PG_INSTRUMENT) {
      snprintf(sh->menu_line[0], LINE_LEN, "PROGRAM NUM");
      snprintf(sh->menu_line[1], LINE_LEN, "< %3u >", (unsigned)s_edit_value);
      snprintf(sh->menu_line[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "");
      snprintf(sh->menu_line[MENU_ROWS - 1], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, " LONG=BACK ");
    } else if (s_page == PG_CC && (s_sel == 0 || s_sel == 1)) {
      snprintf(sh->menu_line[0], LINE_LEN, "MIDI CC CTRL");
      if (s_sel == 0) {
        snprintf(sh->menu_line[1], LINE_LEN, "CC:<%u>CH:%u", (unsigned)s_edit_value,
                 (unsigned)sh->midi_channel);
      } else {
        snprintf(sh->menu_line[1], LINE_LEN, "CC:%u CH:<%u>", (unsigned)sh->cc_number,
                 (unsigned)s_edit_value);
      }
      snprintf(sh->menu_line[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "");
      snprintf(sh->menu_line[MENU_ROWS - 1], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, " LONG=BACK ");
    } else if (s_page == PG_ARP && s_sel == 0) {
      snprintf(sh->menu_line[0], LINE_LEN, "ARP ENABLED");
      snprintf(sh->menu_line[1], LINE_LEN, "< %s >", s_edit_value ? "ON " : "OFF");
      snprintf(sh->menu_line[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "");
      snprintf(sh->menu_line[MENU_ROWS - 1], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, " LONG=BACK ");
    } else if (s_page == PG_ARP && s_sel == 1) {
      snprintf(sh->menu_line[0], LINE_LEN, "ARP RATE");
      snprintf(sh->menu_line[1], LINE_LEN, "< %2u >", (unsigned)s_edit_value);
      snprintf(sh->menu_line[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "1=SLOW16=FAST");
      snprintf(sh->menu_line[MENU_ROWS - 1], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, " LONG=BACK ");
    } else if (s_page == PG_HUB_MIDI && s_sel == 0) {
      static const char *const lab[] = {"USB", "MRG", "DEV"};
      snprintf(sh->menu_line[0], LINE_LEN, "BLE MIDI IN");
      snprintf(sh->menu_line[1], LINE_LEN, "< %3s >", lab[s_edit_value % 3u]);
      snprintf(sh->menu_line[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "");
      snprintf(sh->menu_line[MENU_ROWS - 1], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, " LONG=BACK ");
    } else if (s_page == PG_SETTINGS_AUTO) {
      snprintf(sh->menu_line[0], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "AUTOOFF");
      if (s_edit_value == 0) {
        snprintf(sh->menu_line[1], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "< NEVER >");
      } else {
        snprintf(sh->menu_line[1], LINE_LEN, "< %u MIN >",
                 (unsigned)auto_shutdown_idx_to_minutes(s_edit_value));
      }
      snprintf(sh->menu_line[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "");
      snprintf(sh->menu_line[MENU_ROWS - 1], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, " LONG=BACK ");
    }
    sh->menu_sel = s_sel;
    sh->menu_dirty = true;
    return;
  }

  sync_visible_window();
  /* See menu_render_list_page_into: avoid pcd8544_invert_row for list highlight (proportional font). */
  sh->menu_invert_row = 0xFFu;

  const uint8_t item_rows = menu_item_visible_rows(s_page);
  for (uint8_t row = 0; row < item_rows; row++) {
    uint8_t idx = (uint8_t)(s_first_visible + row);
    if (idx >= page->count) {
      sh->menu_line[row + 1][0] = '\0';
      continue;
    }
    fmt_line(sh->menu_line[row + 1], idx == s_sel ? ">" : " ", page->items[idx]);
  }

  if (s_page == PG_HUB_OSC) {
    snprintf(sh->menu_line[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "NOT IMPLEMENTED");
    snprintf(sh->menu_line[3], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "PLANNED BRIDGE");
    sh->menu_invert_row = 0xFFu;
  } else if (s_page == PG_UTILITY_FILES) {
    snprintf(sh->menu_line[1], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "STORAGE: TODO");
    snprintf(sh->menu_line[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "VOL:SB1STORAGE");
    snprintf(sh->menu_line[3], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "DRAG .MID FILES");
    sh->menu_invert_row = 0xFFu;
  } else if (s_page == PG_UTILITY_DIAG) {
    snprintf(sh->menu_line[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "COMING SOON");
    sh->menu_invert_row = 0xFFu;
  } else if (s_page == PG_SETTINGS_DEVELOPER) {
    snprintf(sh->menu_line[2], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "COMING SOON");
    sh->menu_invert_row = 0xFFu;
  }

  if (s_page == PG_CC) {
    snprintf(sh->menu_line[2], LINE_LEN, "CC:%u CH:%u", (unsigned)sh->cc_number, (unsigned)sh->midi_channel);
  } else if (s_page == PG_ARP) {
    snprintf(sh->menu_line[2], LINE_LEN, "ARP:%s R:%u", sh->arp_enabled ? "ON" : "OFF", (unsigned)sh->arp_rate);
  } else if (s_page == PG_HUB_MIDI) {
    snprintf(sh->menu_line[2], LINE_LEN, "IN:%s", sh->ble_midi_sink == SB1_BLE_MIDI_SINK_USB
                                                    ? "USB"
                                                    : (sh->ble_midi_sink == SB1_BLE_MIDI_SINK_MERGE ? "MRG" : "DEV"));
  }

  if (sh->menu_esc_available) {
    snprintf(sh->menu_line[MENU_ROWS - 1], LINE_LEN, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, " LONG=BACK ");
  }

  sh->menu_sel = s_sel;
  sh->menu_view = SB1_MENU_VIEW_LIST;
  sh->menu_dirty = true;
}
