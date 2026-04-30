/*
 * Display task: Nokia 5110 (PCD8544) — menu mode (3 text rows at 8px) or dashboard + pot.
 */
#include "config.h"
#include "display_task.h"
#include "menu.h"
#include "pcd8544.h"
#include "qrcodegen.h"
#include "sb1_84x48.h"
#include "sb1_livemode_buttons.h"
#include "sb1_verticalmenu.h"
#include "sb1_wireless_meter.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SB1_QR_BUF_LEN 512
/* Fixed on-screen QR edge (pixels). Longer payloads use more QR modules; without this, scaling
 * differs (e.g. Wi‑Fi WIFI:… vs short BLE string) so the two pairing codes looked different sizes. */
#define SB1_QR_EDGE_PX 42u

static uint8_t s_qr_temp[SB1_QR_BUF_LEN];
static uint8_t s_qr_data[SB1_QR_BUF_LEN];
static int s_qr_modules;
static bool s_qr_ready;
static bool s_qr_session_open;
static char s_qr_last_payload[96];

/* Full frame when linked (BT or Wi‑Fi); idle T-only glyph when neither (sb1_wireless_meter_bg). */
static void sb1_background(bool show_bars) {
  pcd8544_clear();
  pcd8544_blit_bands(0, PCD8544_ROWS, show_bars ? sb1_wireless_meter : sb1_wireless_meter_bg);
}

/* Stepped inverted pyramid 5x3 (9 px) for Wi‑Fi — same row as meter art. */
#define SB1_METER_WIFI_RUNE_X 66u
#define SB1_METER_WIFI_RUNE_Y 5u

static void sb1_meter_wifi_rune_draw(void) {
  static const uint8_t pts[][2] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {1, 1}, {2, 1}, {3, 1}, {2, 2}};
  for (size_t i = 0; i < sizeof(pts) / sizeof(pts[0]); i++) {
    pcd8544_set_pixel((uint8_t)(SB1_METER_WIFI_RUNE_X + pts[i][0]), (uint8_t)(SB1_METER_WIFI_RUNE_Y + pts[i][1]),
                       true);
  }
}

/* After text / inverts; show full-reception rune when either wireless link is active. */
static void sb1_meter_link_runes_if(bool bt_connected, bool wifi_connected) {
  if (wifi_connected || bt_connected) {
    sb1_meter_wifi_rune_draw();
  }
}

/* 8px font: show link status as text (Firmware screen line 1). */
static void sb1_fw_status_line(char *buf, size_t n, bool bt_ok, bool wf_ok, bool flash_phase) {
  (void)flash_phase;
  snprintf(buf, n, "BT:%s WF:%s", bt_ok ? "OK" : "OFF", wf_ok ? "OK" : "OFF");
}

/* One 8px text line at `row0`, truncated to DISPLAY_COLS. */
static void sb1_draw_wrapped_one_line(uint8_t row0, const char *nm) {
  char buf[LINE_LEN];
  size_t n = strlen(nm);
  size_t take = n;
  if (take > (size_t)DISPLAY_COLS) {
    take = (size_t)DISPLAY_COLS;
  }
  memcpy(buf, nm, take);
  buf[take] = '\0';
  pcd8544_set_cursor(0, row0);
  pcd8544_print(buf);
}

static void sb1_about_put_row(uint8_t row, const char *text) {
  char buf[LINE_LEN];
  snprintf(buf, sizeof buf, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, text ? text : "");
  pcd8544_set_cursor(0, row);
  pcd8544_print(buf);
}

static void sb1_put_row_bt_disconnect_status(uint8_t row, const char *peer_name) {
  if (!peer_name || peer_name[0] == '\0') {
    sb1_about_put_row(row, "DISCONNECTED");
    return;
  }
  char dn[SB1_BT_PAIR_PEER_DISPLAY_LEN + 1u];
  size_t n = 0;
  while (n < (size_t)SB1_BT_PAIR_PEER_DISPLAY_LEN && peer_name[n] != '\0') {
    dn[n] = peer_name[n];
    n++;
  }
  dn[n] = '\0';
  unsigned name_w = (DISPLAY_COLS > 13u) ? (unsigned)(DISPLAY_COLS - 13u) : 1u;
  char buf[LINE_LEN];
  snprintf(buf, sizeof buf, "DISCONNECTED %.*s", (int)name_w, dn);
  sb1_about_put_row(row, buf);
}

/* Filled disk on top of hollow circles in sb1_livemode_buttons (raw/livemodeButtons.png). */
static void sb1_fill_disk(uint8_t cx, uint8_t cy, uint8_t r) {
  if (r == 0) {
    return;
  }
  int rr = (int)r * (int)r;
  for (int dy = -(int)r; dy <= (int)r; dy++) {
    for (int dx = -(int)r; dx <= (int)r; dx++) {
      if (dx * dx + dy * dy > rr) {
        continue;
      }
      int x = (int)cx + dx;
      int y = (int)cy + dy;
      if (x >= 0 && x < (int)PCD8544_WIDTH && y >= 0 && y < (int)PCD8544_HEIGHT) {
        pcd8544_set_pixel((uint8_t)x, (uint8_t)y, true);
      }
    }
  }
}

/* Row 2: NOTE OFF, or NOTE ON with MIDI note(s) for panel bits (A/B/C = bits 0/1/2). */
static void sb1_live_mode_note_line(char *buf, size_t n, uint8_t bits) {
  if (bits == 0u) {
    snprintf(buf, n, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "NOTE OFF");
    return;
  }
  unsigned v[3];
  unsigned nv = 0;
  if ((bits & 1u) != 0u) {
    v[nv++] = (unsigned)BTN_MIDI_NOTE_A;
  }
  if ((bits & 2u) != 0u) {
    v[nv++] = (unsigned)BTN_MIDI_NOTE_B;
  }
  if ((bits & 4u) != 0u) {
    v[nv++] = (unsigned)BTN_MIDI_NOTE_C;
  }
  char raw[LINE_LEN];
  if (nv == 1u) {
    snprintf(raw, sizeof raw, "NOTE ON %u", v[0]);
  } else if (nv == 2u) {
    snprintf(raw, sizeof raw, "NOTE ON %u,%u", v[0], v[1]);
  } else {
    snprintf(raw, sizeof raw, "NOTE ON %u,%u,%u", v[0], v[1], v[2]);
    if (strlen(raw) > (size_t)DISPLAY_COLS) {
      snprintf(raw, sizeof raw, "N ON %u,%u,%u", v[0], v[1], v[2]);
    }
  }
  snprintf(buf, n, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, raw);
}

/*
 * Background: livemodeButtons.png (BL/BR/TL/TR hollow circles).
 * Rows 0–2: text. Circles live in lower bands; centers tuned to match art — re-run
 * tools/png_to_sb1_livemode_buttons.py after changing raw/livemodeButtons.png.
 * Order: BL=Select, BR=MIDI A, TL=MIDI B, TR=MIDI C (see midi_btn_live bits).
 */
static void sb1_draw_live_mode_screen(bool usb_mounted, uint8_t midi_live_bits, bool bl_pressed,
                                      uint8_t pot_cc, const char *ble_rx_summary, uint32_t ble_rx_last_ms,
                                      uint32_t now_ms) {
  char note_line[LINE_LEN];
  char row2[LINE_LEN];
  char tmp[LINE_LEN];
  pcd8544_clear();
  pcd8544_blit_bands(0, PCD8544_ROWS, sb1_livemode_buttons);
  sb1_about_put_row(0, "LIVE MODE");
  sb1_about_put_row(1, usb_mounted ? "MIDI READY" : "NOT MOUNTED");
  sb1_live_mode_note_line(note_line, sizeof note_line, midi_live_bits);
  if (ble_rx_summary && ble_rx_summary[0] != '\0' && ble_rx_last_ms != 0u && now_ms >= ble_rx_last_ms &&
      (now_ms - ble_rx_last_ms) <= (uint32_t)SB1_BLE_RX_ACTIVITY_MS) {
    snprintf(row2, sizeof row2, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, ble_rx_summary);
  } else if (ble_rx_last_ms != 0u) {
    snprintf(row2, sizeof row2, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, "RX IDLE");
  } else {
    snprintf(tmp, sizeof tmp, "%u:%u %s", (unsigned)POT_MIDI_CC, (unsigned)pot_cc, note_line);
    snprintf(row2, sizeof row2, "%-*.*s", DISPLAY_COLS, DISPLAY_COLS, tmp);
  }
  sb1_about_put_row(2, row2);

  /*
   * [0]=BL [1]=BR [2]=TL [3]=TR — keep the four circles tightly grouped on the
   * livemodeButtons art while preserving one circle per button.
   */
  /* Tuned to ui/sb1_livemode_buttons.h (from raw/livemodeButtons.png); shift down if art changes. */
  static const uint8_t k_live_cx[4] = {10u, 19u, 10u, 19u};
  static const uint8_t k_live_cy[4] = {39u, 39u, 29u, 29u};
  static const uint8_t k_live_r = 4u;
  if (bl_pressed) {
    sb1_fill_disk(k_live_cx[0], k_live_cy[0], k_live_r);
  }
  if ((midi_live_bits & 1u) != 0u) {
    sb1_fill_disk(k_live_cx[1], k_live_cy[1], k_live_r);
  }
  if ((midi_live_bits & 2u) != 0u) {
    sb1_fill_disk(k_live_cx[2], k_live_cy[2], k_live_r);
  }
  if ((midi_live_bits & 4u) != 0u) {
    sb1_fill_disk(k_live_cx[3], k_live_cy[3], k_live_r);
  }
}

/* Centered "YES | NO"; selected word inverted (white on black). */
static void sb1_draw_yes_no_picker(uint8_t text_row, bool select_yes) {
  const char *yes = "YES";
  const char *sep = " | ";
  const char *no = "NO";
  uint16_t wy = pcd8544_text_width(yes);
  uint16_t ws = pcd8544_text_width(sep);
  uint16_t wn = pcd8544_text_width(no);
  uint16_t tot = (uint16_t)(wy + ws + wn);
  uint8_t x0 = 0;
  if (tot < PCD8544_WIDTH) {
    x0 = (uint8_t)(((unsigned)PCD8544_WIDTH - (unsigned)tot) / 2u);
  }
  uint8_t ypx = (uint8_t)((unsigned)text_row * 8u);
  pcd8544_set_cursor(x0, text_row);
  pcd8544_print(yes);
  pcd8544_print(sep);
  pcd8544_print(no);
  uint8_t x_yes = x0;
  uint8_t x_no = (uint8_t)(x0 + wy + ws);
  if (wy > 0u && (uint16_t)x_yes + wy <= PCD8544_WIDTH && ypx + 8u <= PCD8544_HEIGHT) {
    if (select_yes) {
      pcd8544_invert_rect(x_yes, ypx, (uint8_t)wy, 8u);
    } else if (wn > 0u && (uint16_t)x_no + wn <= PCD8544_WIDTH) {
      pcd8544_invert_rect(x_no, ypx, (uint8_t)wn, 8u);
    }
  }
}

/* Extra black row at top, then 3 px frame (matches FIRMWARE confirm modal). */
static void sb1_draw_modal_frame(void) {
  pcd8544_clear();
  for (uint8_t x = 0; x < PCD8544_WIDTH; x++) {
    pcd8544_set_pixel(x, 0, true);
  }
  for (uint8_t t = 0; t < 3u; t++) {
    for (uint8_t y = 0; y < PCD8544_HEIGHT; y++) {
      pcd8544_set_pixel(t, y, true);
      pcd8544_set_pixel((uint8_t)(PCD8544_WIDTH - 1u - t), y, true);
    }
    for (uint8_t x = 0; x < PCD8544_WIDTH; x++) {
      pcd8544_set_pixel(x, (uint8_t)(1u + t), true);
      pcd8544_set_pixel(x, (uint8_t)(PCD8544_HEIGHT - 1u - t), true);
    }
  }
}

static void sb1_draw_tap_tempo_screen(uint16_t bpm, bool frame_on, bool links_up, bool bt_connected,
                                      bool wifi_connected) {
  if (frame_on) {
    sb1_draw_modal_frame();
  } else {
    sb1_background(links_up);
  }

  char bpm_text[16];
  snprintf(bpm_text, sizeof bpm_text, "%uBPM", (unsigned)bpm);
  {
    uint16_t tw = pcd8544_text_width(bpm_text);
    uint8_t x = 0u;
    if (tw < PCD8544_WIDTH) {
      x = (uint8_t)(((unsigned)PCD8544_WIDTH - (unsigned)tw) / 2u);
    }
    pcd8544_set_cursor(x, 3);
    pcd8544_print(bpm_text);
  }

  if (!frame_on) {
    sb1_meter_link_runes_if(bt_connected, wifi_connected);
  }
  pcd8544_display();
}

/* Modal confirm: frame; ARE YOU / SURE? / YES | NO. */
static void sb1_draw_fw_sure_screen(bool select_yes) {
  sb1_draw_modal_frame();
  {
    const char *l1 = "ARE YOU";
    uint16_t w1 = pcd8544_text_width(l1);
    uint8_t xa = (w1 < PCD8544_WIDTH) ? (uint8_t)((PCD8544_WIDTH - w1) / 2u) : 0u;
    pcd8544_set_cursor(xa, 1);
    pcd8544_print(l1);
  }
  {
    const char *l2 = "  SURE?";
    uint16_t w2 = pcd8544_text_width(l2);
    uint8_t xb = (w2 < PCD8544_WIDTH) ? (uint8_t)((PCD8544_WIDTH - w2) / 2u) : 0u;
    pcd8544_set_cursor(xb, 2);
    pcd8544_print(l2);
  }
  sb1_draw_yes_no_picker(3, select_yes);
}

/* SB1_BUILD_* metadata rows; five body lines per page under title. */
#define SB1_ABOUT_BODY_LINES 5u
static const char *const sb1_about_lines[] = {
    SB1_BUILD_GITREVHEAD,
    SB1_BUILD_BUILDNUMBER,
    SB1_BUILD_VERSIONNUMBER,
    SB1_BUILD_FRIENDLYVERSIONNAME,
    SB1_BUILD_VERSIONNAME,
    SB1_BUILD_GITTAG,
    SB1_BUILD_CONDAENVIRONMENTNAME,
    SB1_BUILD_PYTHONVERSION,
    SB1_BUILD_CONDAENVFILE,
    SB1_BUILD_RELEASENAME,
};
#define SB1_ABOUT_LINE_COUNT ((int)(sizeof sb1_about_lines / sizeof sb1_about_lines[0]))
#define SB1_ABOUT_PAGE_COUNT (((SB1_ABOUT_LINE_COUNT) + (int)SB1_ABOUT_BODY_LINES - 1) / (int)SB1_ABOUT_BODY_LINES)

_Static_assert(SB1_ABOUT_INFO_LINE_COUNT == (unsigned)SB1_ABOUT_LINE_COUNT, "SB1_ABOUT_INFO_LINE_COUNT mismatch");

const char *sb1_about_get_line(unsigned idx) {
  if (idx >= (unsigned)SB1_ABOUT_LINE_COUNT) {
    return "";
  }
  return sb1_about_lines[idx];
}

unsigned sb1_about_line_count(void) {
  return (unsigned)SB1_ABOUT_LINE_COUNT;
}

static void sb1_about_fmt_body_line(char *buf, size_t n, int idx, bool selected) {
  char body[LINE_LEN];
  if (idx < 0) {
    body[0] = '\0';
  } else if (idx < SB1_ABOUT_LINE_COUNT) {
    const char *val = sb1_about_lines[idx];
    if (idx == 0) {
      snprintf(body, sizeof body, "1:%s", val);
    } else {
      snprintf(body, sizeof body, "%u:%s", (unsigned)(idx + 1), val);
    }
  } else {
    snprintf(body, sizeof body, "%u:", (unsigned)(idx + 1));
  }
  snprintf(buf, n, "%c%-*.*s", selected ? '>' : ' ', DISPLAY_COLS_VM - 1, DISPLAY_COLS_VM - 1, body);
  buf[DISPLAY_COLS_VM] = '\0';
}

/* Wrap `text` into at most `max_lines` lines <= max_px wide; returns line count. */
static uint8_t sb1_detail_wrap(const char *text, uint16_t max_px, char lines[][24], uint8_t max_lines) {
  const char *p = text ? text : "";
  uint8_t n = 0;
  while (*p != '\0' && n < max_lines) {
    while (*p == ' ') {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    size_t best = 0;
    for (size_t try_len = 1; try_len < sizeof(lines[0]) - 1u && p[try_len - 1] != '\0'; try_len++) {
      char tmp[32];
      size_t tl = try_len;
      if (tl >= sizeof(tmp)) {
        break;
      }
      memcpy(tmp, p, tl);
      tmp[tl] = '\0';
      if (pcd8544_text_width(tmp) <= max_px) {
        best = try_len;
      } else {
        break;
      }
    }
    if (best == 0u) {
      best = 1u;
    } else {
      size_t cut = best;
      for (size_t k = best; k > 1u; k--) {
        if (p[k - 1u] == ' ') {
          cut = k - 1u;
          break;
        }
      }
      if (cut < best) {
        best = cut;
      }
    }
    memcpy(lines[n], p, best);
    lines[n][best] = '\0';
    p += best;
    n++;
  }
  return n;
}

static void sb1_draw_about_detail_screen(const char *text) {
  sb1_draw_modal_frame();
  char rows[5][24];
  uint8_t nlines = sb1_detail_wrap(text, PCD8544_WIDTH - 4u, rows, 5u);
  if (nlines == 0u) {
    nlines = 1u;
    rows[0][0] = '\0';
  }
  uint8_t y0 = (uint8_t)(1u + (5u - nlines) / 2u);
  if (y0 + nlines > 6u) {
    y0 = (uint8_t)(6u - nlines);
  }
  for (uint8_t i = 0; i < nlines; i++) {
    uint16_t w = pcd8544_text_width(rows[i]);
    uint8_t x = 0;
    if (w < PCD8544_WIDTH) {
      x = (uint8_t)(((unsigned)PCD8544_WIDTH - (unsigned)w) / 2u);
    }
    pcd8544_set_cursor(x, (uint8_t)(y0 + i));
    pcd8544_print(rows[i]);
  }
  pcd8544_display();
}

static void sb1_draw_system_about_screen(uint8_t about_line_sel, bool bt_connected, bool links_up,
                                        bool wifi_connected) {
  (void)links_up;
  if (about_line_sel >= (unsigned)SB1_ABOUT_LINE_COUNT) {
    about_line_sel = (uint8_t)(SB1_ABOUT_LINE_COUNT - 1);
  }
  uint8_t page = (uint8_t)((unsigned)about_line_sel / SB1_ABOUT_BODY_LINES);
  if (page >= (unsigned)SB1_ABOUT_PAGE_COUNT) {
    page = (uint8_t)(SB1_ABOUT_PAGE_COUNT - 1u);
  }
  /* No wirelessMeter bitmap — plain background for readability. */
  pcd8544_clear();
  pcd8544_set_cursor(0, 0);
  pcd8544_print("ABOUT");
  int base = (int)page * (int)SB1_ABOUT_BODY_LINES;
  char linebuf[LINE_LEN];
  for (unsigned r = 0; r < SB1_ABOUT_BODY_LINES; r++) {
    int idx = base + (int)r;
    sb1_about_fmt_body_line(linebuf, sizeof linebuf, idx, idx == (int)about_line_sel);
    sb1_about_put_row((uint8_t)(1u + r), linebuf);
  }
  sb1_meter_link_runes_if(bt_connected, wifi_connected);
  sb1_verticalmenu_draw(about_line_sel, (uint8_t)SB1_ABOUT_LINE_COUNT, 1u, SB1_ABOUT_BODY_LINES);
  pcd8544_display();
}

static void sb1_fmt_sel_line(char *dst, size_t dstsz, bool selected, const char *text) {
  snprintf(dst, dstsz, "%s%-*.*s", selected ? ">" : " ", DISPLAY_COLS - 1, DISPLAY_COLS - 1, text);
  if (dstsz > 0) {
    dst[dstsz - 1] = '\0';
  }
}

static void sb1_draw_bt_toast(uint8_t kind, const char *peer_name) {
  sb1_draw_modal_frame();
  if (kind == 1u) {
    sb1_about_put_row(2, "BT CONNECTED");
    if (peer_name && peer_name[0] != '\0') {
      sb1_about_put_row(3, peer_name);
    }
  } else if (kind == 2u) {
    sb1_about_put_row(2, "DISCONNECTED");
    if (peer_name && peer_name[0] != '\0') {
      sb1_about_put_row(3, peer_name);
    }
  }
  pcd8544_display();
}

static const char *sb1_ble_state_tag(uint8_t st) {
  switch (st) {
    case SB1_BLE_STATE_ADVERTISING:
      return "ADV";
    case SB1_BLE_STATE_CONNECTED:
      return "CONN";
    case SB1_BLE_STATE_RECOVERING:
      return "RCV";
    case SB1_BLE_STATE_FAULT:
      return "FLT";
    default:
      return "BOOT";
  }
}

static void sb1_draw_connectivity_screen(uint8_t screen, uint8_t sel, bool connecting_bt, bool connecting_wifi,
                                       bool wifi_scanning, uint8_t wifi_scan_count, const char wifi_ssids[][SB1_WIFI_SSID_MAX + 1u],
                                       const char *wifi_status, bool bt_ok, bool wf_ok, bool flash_on, const char *peer_name,
                                       bool ble_adv_active, uint8_t ble_state, int ble_last_err, int ble_last_disc_reason,
                                       uint32_t ble_recovery_count, uint8_t ble_proto_version) {
  bool show_bars = false;
  if (screen == SB1_CONN_SCREEN_ROOT) {
    show_bars = bt_ok || wf_ok;
  } else if (screen == SB1_CONN_SCREEN_BT || screen == SB1_CONN_SCREEN_BT_DEV) {
    if (connecting_bt && !bt_ok) {
      show_bars = false;
    } else {
      show_bars = bt_ok;
    }
  } else if (screen == SB1_CONN_SCREEN_WIFI || screen == SB1_CONN_SCREEN_WIFI_SCAN) {
    if (connecting_wifi && !wf_ok) {
      show_bars = false;
    } else {
      show_bars = wf_ok;
    }
  } else {
    show_bars = bt_ok || wf_ok;
  }

  if (screen == SB1_CONN_SCREEN_BT) {
    pcd8544_clear();
    if (show_bars) {
      pcd8544_blit_bands(0, PCD8544_ROWS, sb1_wireless_meter);
    }
  } else {
    sb1_background(show_bars);
  }

  char ln[LINE_LEN];

  if (screen == SB1_CONN_SCREEN_ROOT) {
    sb1_about_put_row(0, "CONNECTIONS");
    sb1_fmt_sel_line(ln, sizeof ln, sel == 0, "BLUETOOTH");
    sb1_about_put_row(1, ln);
    sb1_fmt_sel_line(ln, sizeof ln, sel == 1, "WIFI");
    sb1_about_put_row(2, ln);
    sb1_about_put_row(5, "HOLD=EXIT");
  } else if (screen == SB1_CONN_SCREEN_BT) {
    if (connecting_bt && !bt_ok) {
      /* 0 = ADVERTISING blink line, 1 = TURNOFF */
      sb1_about_put_row(0, "BLUETOOTH");
      sb1_about_put_row(1, "");
      if (sel == 1u) {
        sb1_fmt_sel_line(ln, sizeof ln, false, "ADVERTISING...");
        sb1_about_put_row(2, ln);
      } else if (flash_on) {
        sb1_fmt_sel_line(ln, sizeof ln, true, "ADVERTISING...");
        sb1_about_put_row(2, ln);
      } else {
        sb1_about_put_row(2, "");
      }
      sb1_about_put_row(3, "");
      sb1_fmt_sel_line(ln, sizeof ln, sel == 1u, "TURNOFF");
      sb1_about_put_row(4, ln);
      sb1_about_put_row(5, "LONG=BACK");
    } else {
      const bool bt_power_on = bt_ok || connecting_bt || ble_adv_active;
      static const char *const bt_main_on[5] = {"ADVERTISE", "QRCODE", "DEV", "DISCONNECT", "TURNOFF"};
      static const char *const bt_main_off[5] = {"ADVERTISE", "QRCODE", "DEV", "DISCONNECT", "TURNON"};
      const char *const *bt_main = bt_power_on ? bt_main_on : bt_main_off;
      const uint8_t bt_scroll_count = 5u;
      const uint8_t bt_sel_count = 5u;
      if (sel >= bt_sel_count) {
        sel = (uint8_t)(bt_sel_count - 1u);
      }
      uint8_t pick = sel;
      uint8_t first_visible = 0u;
      if (pick > 1u) {
        first_visible = (uint8_t)(pick - 1u);
      }
      if ((uint8_t)(first_visible + 3u) > bt_scroll_count) {
        first_visible = (uint8_t)(bt_scroll_count - 3u);
      }

      sb1_about_put_row(0, "BLUETOOTH");

      /* Row 1: connection status (not part of scrollbar) */
      if (bt_ok) {
        char shortname[SB1_BT_PAIR_PEER_DISPLAY_LEN + 1u];
        size_t n = 0;
        while (peer_name && n < (size_t)SB1_BT_PAIR_PEER_DISPLAY_LEN && peer_name[n] != '\0') {
          shortname[n] = peer_name[n];
          n++;
        }
        shortname[n] = '\0';
        sb1_about_put_row(1, n > 0u ? shortname : "BT CONNECTED");
      } else {
        sb1_put_row_bt_disconnect_status(1u, peer_name);
      }

      sb1_fmt_sel_line(ln, sizeof ln, pick == first_visible, bt_main[first_visible]);
      sb1_about_put_row(2, ln);
      if ((uint8_t)(first_visible + 1u) < bt_scroll_count) {
        sb1_fmt_sel_line(ln, sizeof ln, pick == (uint8_t)(first_visible + 1u), bt_main[first_visible + 1u]);
        sb1_about_put_row(3, ln);
      }
      if ((uint8_t)(first_visible + 2u) < bt_scroll_count) {
        sb1_fmt_sel_line(ln, sizeof ln, pick == (uint8_t)(first_visible + 2u), bt_main[first_visible + 2u]);
        sb1_about_put_row(4, ln);
      }
      sb1_about_put_row(5, "LONG=BACK");
      if (bt_scroll_count >= 3u) {
        sb1_verticalmenu_draw(sel, bt_sel_count, 2u, 3u);
      }
    }
  } else if (screen == SB1_CONN_SCREEN_BT_DEV) {
    sb1_about_put_row(0, "BT DEV");
    sb1_put_row_bt_disconnect_status(1u, NULL);
    snprintf(ln, sizeof ln, "ADV:%s ST:%s", ble_adv_active ? "ON" : "OFF", sb1_ble_state_tag(ble_state));
    sb1_about_put_row(2, ln);
    snprintf(ln, sizeof ln, "ERR:%d DISC:%d", ble_last_err, ble_last_disc_reason);
    sb1_about_put_row(3, ln);
    snprintf(ln, sizeof ln, "R:%u P:%u", (unsigned)ble_recovery_count, (unsigned)ble_proto_version);
    sb1_about_put_row(4, ln);
    sb1_about_put_row(5, "LONG=BACK");
  } else if (screen == SB1_CONN_SCREEN_WIFI) {
    if (connecting_wifi && !wf_ok) {
      sb1_about_put_row(0, "WIFI");
      sb1_about_put_row(1, "");
      if (flash_on) {
        sb1_about_put_row(2, "CONNECTING...");
      } else {
        sb1_about_put_row(2, "");
      }
      sb1_about_put_row(5, "LONG=BACK");
    } else {
      sb1_about_put_row(0, "WIFI");
      const char *row1 = wf_ok ? "CONNECTED" : "SCAN";
      sb1_fmt_sel_line(ln, sizeof ln, sel == 0, row1);
      sb1_about_put_row(1, ln);
      sb1_fmt_sel_line(ln, sizeof ln, sel == 1, "QR CODE");
      sb1_about_put_row(2, ln);
      sb1_about_put_row(5, "LONG=BACK");
    }
  } else if (screen == SB1_CONN_SCREEN_WIFI_SCAN) {
    sb1_about_put_row(0, "WIFI SCAN");
    if (wifi_scanning) {
      if (flash_on) {
        sb1_about_put_row(2, "SCANNING...");
      } else {
        sb1_about_put_row(2, "");
      }
    } else if (wifi_scan_count == 0u) {
      sb1_about_put_row(2, "NO HOTSPOTS");
    } else {
      uint8_t pick = sel;
      if (pick >= wifi_scan_count) {
        pick = (uint8_t)(wifi_scan_count - 1u);
      }
      uint8_t first_visible = 0u;
      if (pick > 1u) {
        first_visible = (uint8_t)(pick - 1u);
      }
      if ((uint8_t)(first_visible + 3u) > wifi_scan_count) {
        first_visible = (uint8_t)(wifi_scan_count - 3u);
      }
      sb1_fmt_sel_line(ln, sizeof ln, pick == first_visible, wifi_ssids[first_visible]);
      sb1_about_put_row(1, ln);
      if ((uint8_t)(first_visible + 1u) < wifi_scan_count) {
        sb1_fmt_sel_line(ln, sizeof ln, pick == (uint8_t)(first_visible + 1u), wifi_ssids[first_visible + 1u]);
        sb1_about_put_row(2, ln);
      }
      if ((uint8_t)(first_visible + 2u) < wifi_scan_count) {
        sb1_fmt_sel_line(ln, sizeof ln, pick == (uint8_t)(first_visible + 2u), wifi_ssids[first_visible + 2u]);
        sb1_about_put_row(3, ln);
      }
      if (wifi_scan_count >= 3u) {
        sb1_verticalmenu_draw(pick, wifi_scan_count, 1u, 3u);
      }
    }
    if (wifi_status && wifi_status[0] != '\0') {
      sb1_about_put_row(4, wifi_status);
    }
    sb1_about_put_row(5, "LONG=BACK");
  }

  if (screen != SB1_CONN_SCREEN_BT && screen != SB1_CONN_SCREEN_BT_DEV) {
    sb1_meter_link_runes_if(bt_ok, wf_ok);
  }
  pcd8544_display();
}

static void sb1_draw_qr_screen(const char *payload, bool links_up, bool bt_connected, bool wifi_connected) {
  const char *pl = (payload && payload[0] != '\0') ? payload : SB1_BT_QR_PAYLOAD;
  if (!s_qr_session_open || strcmp(s_qr_last_payload, pl) != 0) {
    strncpy(s_qr_last_payload, pl, sizeof s_qr_last_payload - 1);
    s_qr_last_payload[sizeof s_qr_last_payload - 1] = '\0';
    /* Keep QR versions small so the rendered code stays physically large on 84x48 LCD. */
    s_qr_ready = qrcodegen_encodeText(s_qr_last_payload, s_qr_temp, s_qr_data, qrcodegen_Ecc_LOW, 1, 4,
                                      qrcodegen_Mask_AUTO, false);
    s_qr_modules = s_qr_ready ? qrcodegen_getSize(s_qr_data) : 0;
    s_qr_session_open = true;
  }

  sb1_background(links_up);
  if (!s_qr_ready || s_qr_modules <= 0) {
    pcd8544_set_cursor(0, 0);
    pcd8544_print("QR ENCODE");
    pcd8544_set_cursor(0, 1);
    pcd8544_print("FAILED");
    sb1_meter_link_runes_if(bt_connected, wifi_connected);
    pcd8544_display();
    return;
  }

  const int modules = s_qr_modules;
  const int edge = (int)SB1_QR_EDGE_PX;
  int ox = ((int)PCD8544_WIDTH - edge) / 2;
  if (ox < 0) {
    ox = 0;
  }
  int oy = ((int)PCD8544_HEIGHT - edge) / 2;
  if (oy < 0) {
    oy = 0;
  }

  for (int py = 0; py < edge; py++) {
    for (int px = 0; px < edge; px++) {
      int mx = (px * modules) / edge;
      int my = (py * modules) / edge;
      if (mx >= modules) {
        mx = modules - 1;
      }
      if (my >= modules) {
        my = modules - 1;
      }
      if (!qrcodegen_getModule(s_qr_data, mx, my)) {
        continue;
      }
      int pxv = ox + px;
      int pyv = oy + py;
      if (pxv >= 0 && pxv < (int)PCD8544_WIDTH && pyv >= 0 && pyv < (int)PCD8544_HEIGHT) {
        pcd8544_set_pixel((uint8_t)pxv, (uint8_t)pyv, true);
      }
    }
  }
  /* Keep QR area visually clean; extra glyphs can make the code look non-square. */
  (void)bt_connected;
  (void)wifi_connected;
  pcd8544_display();
}

static void display_task_fn(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;
  char last_line4[LINE_LEN] = "";
  char last_line5[LINE_LEN] = "";
  char last_menu[MENU_ROWS][LINE_LEN];
  uint8_t last_menu_invert = 0xFFu;
  uint16_t last_pot = 0xFFFFu;
  uint8_t last_pot_cc = 0xFFu;
  uint8_t last_midi_btn = 0xFFu;
  bool was_menu_active = false;
  static bool s_disp_bt_last = false;
  static uint8_t s_disp_c_screen = 0xFFu;
  static uint8_t s_disp_c_sel = 0xFFu;
  static bool s_disp_c_qr = false;
  static bool s_disp_c_qr_wifi = false;
  static bool s_disp_c_con_bt = false;
  static bool s_disp_c_con_wf = false;
  static uint8_t s_disp_c_flash = 0xFFu;
  static bool s_last_bt_peer_conn = false;
  static char s_last_bt_peer_name[BT_PEER_NAME_MAX];
  static uint8_t s_last_menu_view_paint = 0xFFu;
  static uint8_t s_last_about_line_sel = 0xFFu;
  static char s_last_about_detail_text[SB1_ABOUT_DETAIL_BUF];
  /* Repaint when bt_peer_connected toggles (meter runes / link-dependent UI). */
  static bool s_last_bt_icon_state = false;
  /* FIRMWARE: flash animation + last link snapshot (decoupled from other screens). */
  static bool s_fw_last_bt = false;
  static bool s_fw_last_wf = false;
  static uint8_t s_fw_last_flash = 0xFFu;
  static uint8_t s_msc_mount_flash = 0xFFu;
  static unsigned s_last_ota_fill = 0xFFFFu;
  static uint8_t s_last_fw_yes_sel = 0xFFu;
  static uint8_t s_last_sure_yes_sel = 0xFFu;
  static uint8_t s_last_tap_br_pressed = 0xFFu;
  static uint16_t s_last_tap_bpm = 0xFFFFu;
  /* Meter bars visibility tracks BT or Wi‑Fi up; repaint when it changes without other dirty flags. */
  static bool s_last_links_up = false;
  static uint8_t s_last_bt_toast_kind = 0xFFu;

  memset(last_menu, 0, sizeof(last_menu));
  s_last_bt_peer_name[0] = '\0';
  memset(s_last_about_detail_text, 0, sizeof s_last_about_detail_text);

  pcd8544_init(PIN_SCLK, PIN_DIN, PIN_DC, PIN_CS, PIN_RST);
  pcd8544_set_contrast(55);
  /* Branded splash once at boot; runtime uses sb1_background(links_up) for meter bars. */
  pcd8544_clear();
  pcd8544_blit_bands(0, PCD8544_ROWS, sb1_84x48);
  pcd8544_display();
  vTaskDelay(pdMS_TO_TICKS(1200));

  for (;;) {
    char line4[LINE_LEN] = "---";
    char line5[LINE_LEN] = "---";
    char potline[LINE_LEN];
    char menu_copy[MENU_ROWS][LINE_LEN];
    uint16_t pot = 0;
    uint8_t pot_cc = 0;
    bool menu_active = false;
    uint8_t menu_view = SB1_MENU_VIEW_LIST;
    bool menu_parent_preview = false;
    bool menu_dirty = false;
    uint8_t menu_invert_row = 0xFFu;
    uint8_t midi_btn = 0;
    bool bt_pairing = false;
    uint8_t conn_screen = 0;
    uint8_t conn_sel = 0;
    bool conn_qr = false;
    bool conn_qr_wifi = false;
    bool conn_cbt = false;
    bool conn_cwf = false;
    bool conn_wscan = false;
    uint8_t conn_wscan_count = 0u;
    bool bt_dirty = false;
    bool bt_peer_connected = false;
    bool wifi_sta_connected = false;
    bool ble_adv_active = false;
    uint8_t ble_state = SB1_BLE_STATE_BOOTING;
    int ble_last_err = 0;
    int ble_last_disc_reason = 0;
    uint32_t ble_recovery_count = 0u;
    uint8_t ble_proto_version = 0u;
    uint32_t bt_toast_until_ms = 0u;
    uint8_t bt_toast_kind = 0u;
    bool live_mode_active = false;
    bool usb_mounted = false;
    bool usb_msc_mounting = false;
    bool usb_msc_medium_ready = false;
    int bl_button = 1;
    uint16_t tap_bpm = 120u;
    char bt_peer_name[BT_PEER_NAME_MAX];
    char ble_rx_summary[LINE_LEN];
    bt_peer_name[0] = '\0';
    ble_rx_summary[0] = '\0';
    uint32_t ble_rx_last_ms = 0u;
    uint8_t about_line_sel_copy = 0;
    char conn_wifi_ssids[SB1_WIFI_SCAN_MAX_RESULTS][SB1_WIFI_SSID_MAX + 1u];
    char conn_wifi_status[LINE_LEN];
    char about_detail_copy[SB1_ABOUT_DETAIL_BUF];
    about_detail_copy[0] = '\0';
    uint32_t msc_attached_until_ms = 0u;
    if (sh && sh->mutex) {
      if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        midi_btn = sh->midi_btn_live;
        if (sh->button_state) {
          bl_button = *sh->button_state;
        }
        strncpy(line4, sh->line4, LINE_LEN - 1);
        line4[LINE_LEN - 1] = '\0';
        strncpy(line5, sh->line5, LINE_LEN - 1);
        line5[LINE_LEN - 1] = '\0';
        pot = sh->pot_raw;
        pot_cc = sh->pot_cc_127;
        menu_active = sh->menu_active;
        menu_view = sh->menu_view;
        menu_parent_preview = sh->menu_parent_preview;
        menu_dirty = sh->menu_dirty;
        menu_invert_row = sh->menu_invert_row;
        memcpy(menu_copy, sh->menu_line, sizeof(menu_copy));
        bt_pairing = sh->bt_pairing_active;
        conn_screen = sh->connectivity_screen;
        conn_sel = sh->connectivity_sel;
        conn_qr = sh->connectivity_qr_visible;
        conn_qr_wifi = sh->connectivity_qr_wifi;
        conn_cbt = sh->connectivity_connecting_bt;
        conn_cwf = sh->connectivity_connecting_wifi;
        conn_wscan = sh->connectivity_wifi_scanning;
        conn_wscan_count = sh->connectivity_wifi_scan_count;
        memcpy(conn_wifi_ssids, sh->connectivity_wifi_ssids, sizeof(conn_wifi_ssids));
        memcpy(conn_wifi_status, sh->connectivity_wifi_status, sizeof(conn_wifi_status));
        bt_dirty = sh->bt_pairing_dirty;
        bt_peer_connected = sh->bt_peer_connected;
        wifi_sta_connected = sh->wifi_sta_connected;
        ble_adv_active = sh->ble_adv_active;
        ble_state = sh->ble_state;
        ble_last_err = sh->ble_last_err;
        ble_last_disc_reason = sh->ble_last_disc_reason;
        ble_recovery_count = sh->ble_recovery_count;
        ble_proto_version = sh->ble_proto_version;
        bt_toast_until_ms = sh->bt_toast_until_ms;
        bt_toast_kind = sh->bt_toast_kind;
        strncpy(bt_peer_name, sh->bt_peer_name, BT_PEER_NAME_MAX - 1);
        bt_peer_name[BT_PEER_NAME_MAX - 1] = '\0';
        strncpy(ble_rx_summary, sh->ble_rx_last_summary, LINE_LEN - 1);
        ble_rx_summary[LINE_LEN - 1] = '\0';
        ble_rx_last_ms = sh->ble_rx_last_ms;
        live_mode_active = sh->live_mode_active;
        usb_mounted = sh->usb_mounted;
        usb_msc_mounting = sh->usb_msc_mounting;
        usb_msc_medium_ready = sh->usb_msc_medium_ready;
        tap_bpm = sh->tap_bpm;
        about_line_sel_copy = sh->about_line_sel;
        memcpy(about_detail_copy, sh->about_detail_text, sizeof about_detail_copy);
        msc_attached_until_ms = sh->usb_msc_attached_until_ms;
        xSemaphoreGive(sh->mutex);
      }
    }

    {
      uint32_t now_ms = to_ms_since_boot(get_absolute_time());
      if (msc_attached_until_ms != 0u && now_ms < msc_attached_until_ms) {
        pcd8544_clear();
        const char *ln1 = "SB1STORAGE";
        const char *ln2 = "ATTACHED";
        uint16_t w1 = pcd8544_text_width(ln1);
        uint16_t w2 = pcd8544_text_width(ln2);
        uint8_t x1 = (w1 < PCD8544_WIDTH) ? (uint8_t)((PCD8544_WIDTH - w1) / 2u) : 0u;
        uint8_t x2 = (w2 < PCD8544_WIDTH) ? (uint8_t)((PCD8544_WIDTH - w2) / 2u) : 0u;
        pcd8544_set_cursor(x1, 2u);
        pcd8544_print(ln1);
        pcd8544_set_cursor(x2, 3u);
        pcd8544_print(ln2);
        pcd8544_display();
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
        continue;
      }
    }

    bool links_up = bt_peer_connected || wifi_sta_connected;
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    if (bt_toast_until_ms != 0u && now_ms < bt_toast_until_ms && !menu_active && !bt_pairing) {
      if (bt_toast_kind != s_last_bt_toast_kind) {
        sb1_draw_bt_toast(bt_toast_kind, bt_peer_name);
        s_last_bt_toast_kind = bt_toast_kind;
      }
      vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
      continue;
    }
    s_last_bt_toast_kind = 0xFFu;

    if (bt_pairing) {
      bool peer_changed = (bt_peer_connected != s_last_bt_peer_conn) ||
                          (strncmp(bt_peer_name, s_last_bt_peer_name, BT_PEER_NAME_MAX) != 0);
      bool flash_phase =
          ((to_ms_since_boot(get_absolute_time()) / (uint32_t)SB1_CONN_CONNECTING_FLASH_MS) & 1u) != 0;
      bool connecting_any =
          (conn_cbt && !bt_peer_connected) || (conn_cwf && !wifi_sta_connected);
      /* Same 400 ms blink as "ADVERTISING..." / "CONNECTING...": includes Wi‑Fi scan spinner. */
      bool flash_animation = connecting_any || conn_wscan;
      uint8_t flash_byte = flash_phase ? 1u : 0u;
      bool need = bt_dirty || !s_disp_bt_last || conn_screen != s_disp_c_screen ||
                  conn_sel != s_disp_c_sel || conn_qr != s_disp_c_qr ||
                  conn_qr_wifi != s_disp_c_qr_wifi || conn_cbt != s_disp_c_con_bt ||
                  conn_cwf != s_disp_c_con_wf || peer_changed || (links_up != s_last_links_up);
      if (flash_animation && flash_byte != s_disp_c_flash) {
        need = true;
      }
      if (need) {
        if (conn_qr) {
          const char *qp = conn_qr_wifi ? SB1_WIFI_QR_PAYLOAD : SB1_BT_QR_PAYLOAD;
          sb1_draw_qr_screen(qp, links_up, bt_peer_connected, wifi_sta_connected);
        } else {
          sb1_draw_connectivity_screen(conn_screen, conn_sel, conn_cbt, conn_cwf, conn_wscan, conn_wscan_count,
                                      conn_wifi_ssids, conn_wifi_status, bt_peer_connected,
                                      wifi_sta_connected, flash_phase, bt_peer_name, ble_adv_active, ble_state,
                                      ble_last_err, ble_last_disc_reason,
                                      ble_recovery_count, ble_proto_version);
        }
        s_disp_bt_last = true;
        s_disp_c_screen = conn_screen;
        s_disp_c_sel = conn_sel;
        s_disp_c_qr = conn_qr;
        s_disp_c_qr_wifi = conn_qr_wifi;
        s_disp_c_con_bt = conn_cbt;
        s_disp_c_con_wf = conn_cwf;
        s_disp_c_flash = flash_byte;
        s_last_bt_peer_conn = bt_peer_connected;
        strncpy(s_last_bt_peer_name, bt_peer_name, BT_PEER_NAME_MAX - 1);
        s_last_bt_peer_name[BT_PEER_NAME_MAX - 1] = '\0';
        s_last_links_up = links_up;
        if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          sh->bt_pairing_dirty = false;
          xSemaphoreGive(sh->mutex);
        }
      }
      was_menu_active = false;
      vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
      continue;
    }

    if (s_disp_bt_last) {
      s_disp_bt_last = false;
      s_disp_c_screen = 0xFFu;
      s_disp_c_sel = 0xFFu;
      s_disp_c_qr = false;
      s_disp_c_qr_wifi = false;
      s_disp_c_con_bt = false;
      s_disp_c_con_wf = false;
      s_disp_c_flash = 0xFFu;
      s_qr_last_payload[0] = '\0';
      s_qr_session_open = false;
      s_qr_ready = false;
      s_qr_modules = 0;
    }
    snprintf(potline, LINE_LEN, "POT:%4u CC%3u", (unsigned)pot, (unsigned)pot_cc);
    char btnline[LINE_LEN];
    /* 14 cols max — * = pressed, . = open; order = GPIO3 BR, 4 TL, 5 TR */
    snprintf(btnline, LINE_LEN, "345:%c%c%c",
             (midi_btn & 1u) ? '*' : '.',
             (midi_btn & 2u) ? '*' : '.',
             (midi_btn & 4u) ? '*' : '.');

    if (menu_active) {
      if (menu_parent_preview) {
        char paint_menu[MENU_ROWS][LINE_LEN];
        uint8_t paint_invert = 0xFFu;
        if (menu_parent_preview_lines(sh, paint_menu, &paint_invert)) {
          s_last_menu_view_paint = SB1_MENU_VIEW_LIST;
          bool menu_has_content = false;
          for (int i = 0; i < MENU_ROWS; i++) {
            if (paint_menu[i][0] != '\0') {
              menu_has_content = true;
              break;
            }
          }
          bool need = menu_dirty || (menu_has_content && last_menu[0][0] == '\0') ||
                      (bt_peer_connected != s_last_bt_icon_state) || (links_up != s_last_links_up);
          if (!need) {
            if (paint_invert != last_menu_invert) {
              need = true;
            }
            for (int i = 0; i < MENU_ROWS; i++) {
              if (strcmp(paint_menu[i], last_menu[i]) != 0) {
                need = true;
                break;
              }
            }
          }
          if (need) {
            sb1_background(links_up);
            for (int r = 0; r < MENU_ROWS; r++) {
              pcd8544_set_cursor(0, (uint8_t)r);
              pcd8544_print(paint_menu[r]);
            }
            if (paint_invert < MENU_ROWS) {
              pcd8544_invert_row(paint_invert);
            }
            sb1_meter_link_runes_if(bt_peer_connected, wifi_sta_connected);
            pcd8544_display();
            s_last_bt_icon_state = bt_peer_connected;
            s_last_links_up = links_up;
            memcpy(last_menu, paint_menu, sizeof(last_menu));
            last_menu_invert = paint_invert;
            if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
              sh->menu_dirty = false;
              xSemaphoreGive(sh->mutex);
            }
          }
          was_menu_active = true;
          vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
          continue;
        }
      }

      if (menu_view == SB1_MENU_VIEW_TAP_TEMPO) {
        bool br_pressed = (midi_btn & 1u) != 0u;
        uint8_t br_state = br_pressed ? 1u : 0u;
        bool need_tap = menu_dirty || (s_last_menu_view_paint != SB1_MENU_VIEW_TAP_TEMPO) ||
                        (br_state != s_last_tap_br_pressed) || (tap_bpm != s_last_tap_bpm) ||
                        (!br_pressed && (links_up != s_last_links_up));
        if (need_tap) {
          sb1_draw_tap_tempo_screen(tap_bpm, br_pressed, links_up, bt_peer_connected, wifi_sta_connected);
          s_last_menu_view_paint = SB1_MENU_VIEW_TAP_TEMPO;
          s_last_tap_br_pressed = br_state;
          s_last_tap_bpm = tap_bpm;
          s_last_links_up = links_up;
          if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            sh->menu_dirty = false;
            xSemaphoreGive(sh->mutex);
          }
        }
        was_menu_active = true;
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
        continue;
      }

      if (menu_view == SB1_MENU_VIEW_SYSTEM_FW_OTA) {
        TickType_t now = xTaskGetTickCount();
        uint32_t t = (uint32_t)now / pdMS_TO_TICKS(400);
        unsigned fill = (unsigned)(t % 15u);
        bool need_ota = menu_dirty || (s_last_menu_view_paint != SB1_MENU_VIEW_SYSTEM_FW_OTA) ||
                        (fill != s_last_ota_fill) || (links_up != s_last_links_up);
        if (need_ota) {
          sb1_background(links_up);
          pcd8544_set_cursor(0, 0);
          pcd8544_print("DO NOT");
          pcd8544_set_cursor(0, 1);
          pcd8544_print("TURN OFF");
          {
            char bar[LINE_LEN];
            bar[0] = '[';
            for (unsigned i = 0; i < 12u; i++) {
              bar[1u + i] = (i < fill) ? '=' : '.';
            }
            bar[13] = ']';
            bar[14] = '\0';
            pcd8544_set_cursor(0, 2);
            pcd8544_print(bar);
          }
          sb1_meter_link_runes_if(bt_peer_connected, wifi_sta_connected);
          pcd8544_display();
          s_last_menu_view_paint = SB1_MENU_VIEW_SYSTEM_FW_OTA;
          s_last_ota_fill = fill;
          s_last_links_up = links_up;
          if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            sh->menu_dirty = false;
            xSemaphoreGive(sh->mutex);
          }
        }
        was_menu_active = true;
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
        continue;
      }

      if (menu_view == SB1_MENU_VIEW_SYSTEM_ABOUT_DETAIL) {
        bool need_d =
            menu_dirty || (s_last_menu_view_paint != SB1_MENU_VIEW_SYSTEM_ABOUT_DETAIL) ||
            (memcmp(about_detail_copy, s_last_about_detail_text, SB1_ABOUT_DETAIL_BUF) != 0);
        if (need_d) {
          sb1_draw_about_detail_screen(about_detail_copy);
          s_last_menu_view_paint = SB1_MENU_VIEW_SYSTEM_ABOUT_DETAIL;
          memcpy(s_last_about_detail_text, about_detail_copy, sizeof s_last_about_detail_text);
          if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            sh->menu_dirty = false;
            xSemaphoreGive(sh->mutex);
          }
        }
        was_menu_active = true;
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
        continue;
      }

      if (menu_view == SB1_MENU_VIEW_SYSTEM_ABOUT) {
        uint8_t al = about_line_sel_copy;
        bool need_about = menu_dirty || (s_last_menu_view_paint != SB1_MENU_VIEW_SYSTEM_ABOUT) ||
                          (al != s_last_about_line_sel) ||
                          (bt_peer_connected != s_last_bt_icon_state) || (links_up != s_last_links_up);
        if (need_about) {
          sb1_draw_system_about_screen(al, bt_peer_connected, links_up, wifi_sta_connected);
          s_last_menu_view_paint = SB1_MENU_VIEW_SYSTEM_ABOUT;
          s_last_about_line_sel = al;
          if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            sh->menu_dirty = false;
            xSemaphoreGive(sh->mutex);
          }
          s_last_bt_icon_state = bt_peer_connected;
          s_last_links_up = links_up;
        }
        was_menu_active = true;
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
        continue;
      }

      if (menu_view == SB1_MENU_VIEW_SYSTEM_FW_SURE) {
        bool select_yes = (pot < 2048u);
        uint8_t ys = select_yes ? 1u : 0u;
        bool need_sure = menu_dirty || (s_last_menu_view_paint != SB1_MENU_VIEW_SYSTEM_FW_SURE) ||
                         (ys != s_last_sure_yes_sel);
        if (need_sure) {
          sb1_draw_fw_sure_screen(select_yes);
          pcd8544_display();
          s_last_menu_view_paint = SB1_MENU_VIEW_SYSTEM_FW_SURE;
          s_last_sure_yes_sel = ys;
          if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            sh->menu_dirty = false;
            xSemaphoreGive(sh->mutex);
          }
        }
        was_menu_active = true;
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
        continue;
      }

      if (menu_view == SB1_MENU_VIEW_SYSTEM_FW) {
        char paint_fw[MENU_ROWS][LINE_LEN];
        memcpy(paint_fw, menu_copy, sizeof(paint_fw));
        bool flash_phase =
            (((uint32_t)xTaskGetTickCount() / pdMS_TO_TICKS(250)) & 1u) != 0u;
        bool select_yes = (pot < 2048u);
        uint8_t ys = select_yes ? 1u : 0u;
        bool need_fw = menu_dirty || (s_last_menu_view_paint != SB1_MENU_VIEW_SYSTEM_FW) ||
                       (bt_peer_connected != s_fw_last_bt) || (wifi_sta_connected != s_fw_last_wf) ||
                       (links_up != s_last_links_up) || (ys != s_last_fw_yes_sel) ||
                       (((!bt_peer_connected) || (!wifi_sta_connected)) && (flash_phase != s_fw_last_flash));
        if (need_fw) {
          char linkln[LINE_LEN];
          sb1_background(links_up);
          sb1_about_put_row(0, paint_fw[0]);
          sb1_fw_status_line(linkln, sizeof linkln, bt_peer_connected, wifi_sta_connected, flash_phase);
          pcd8544_set_cursor(0, 1);
          pcd8544_print(linkln);
          sb1_about_put_row(2, paint_fw[2]);
          if (links_up && paint_fw[2][0] != '\0') {
            sb1_draw_yes_no_picker(3, select_yes);
          } else {
            sb1_about_put_row(3, "");
          }
          sb1_about_put_row(4, "");
          sb1_about_put_row((uint8_t)(MENU_ROWS - 1u), paint_fw[MENU_ROWS - 1]);
          sb1_meter_link_runes_if(bt_peer_connected, wifi_sta_connected);
          pcd8544_display();
          s_last_menu_view_paint = SB1_MENU_VIEW_SYSTEM_FW;
          s_fw_last_bt = bt_peer_connected;
          s_fw_last_wf = wifi_sta_connected;
          s_fw_last_flash = flash_phase ? 1u : 0u;
          s_last_fw_yes_sel = ys;
          s_last_links_up = links_up;
          if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            sh->menu_dirty = false;
            xSemaphoreGive(sh->mutex);
          }
        }
        was_menu_active = true;
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
        continue;
      }
      s_last_menu_view_paint = SB1_MENU_VIEW_LIST;

      char paint_menu[MENU_ROWS][LINE_LEN];
      memcpy(paint_menu, menu_copy, sizeof(paint_menu));
      bool mount_flash_phase = false;
      if (strcmp(paint_menu[0], "MIDI FILES") == 0) {
        bool mount_selected = (paint_menu[1][0] == '>');
        if (usb_msc_mounting && !usb_msc_medium_ready) {
          mount_flash_phase = ((to_ms_since_boot(get_absolute_time()) / 250u) & 1u) != 0u;
          snprintf(paint_menu[1], LINE_LEN, "%s%-*.*s", mount_selected ? ">" : " ",
                   DISPLAY_COLS - 1, DISPLAY_COLS - 1, mount_flash_phase ? "MOUNTING" : "");
        } else if (usb_msc_medium_ready) {
          snprintf(paint_menu[1], LINE_LEN, "%s%-*.*s", mount_selected ? ">" : " ",
                   DISPLAY_COLS - 1, DISPLAY_COLS - 1, "MOUNTED");
        } else {
          snprintf(paint_menu[1], LINE_LEN, "%s%-*.*s", mount_selected ? ">" : " ",
                   DISPLAY_COLS - 1, DISPLAY_COLS - 1, "MOUNT");
        }
      }

      /* If ui_task hasn't rendered yet, menu lines can still be empty while menu_dirty is false;
       * never skip a paint when we have nothing cached (fixes one-frame race / permanent blank). */
      bool menu_has_content = false;
      for (int i = 0; i < MENU_ROWS; i++) {
        if (menu_copy[i][0] != '\0') {
          menu_has_content = true;
          break;
        }
      }
      bool need = menu_dirty || (menu_has_content && last_menu[0][0] == '\0') ||
                  (bt_peer_connected != s_last_bt_icon_state) || (links_up != s_last_links_up);
      if (!need) {
        if (usb_msc_mounting && !usb_msc_medium_ready) {
          uint8_t flash_byte = mount_flash_phase ? 1u : 0u;
          if (flash_byte != s_msc_mount_flash) {
            need = true;
          }
        } else if (s_msc_mount_flash != 0xFFu) {
          need = true;
        }
        if (menu_invert_row != last_menu_invert) {
          need = true;
        }
        for (int i = 0; i < MENU_ROWS; i++) {
          if (strcmp(paint_menu[i], last_menu[i]) != 0) {
            need = true;
            break;
          }
        }
      }
      if (need) {
        sb1_background(links_up);
        for (int r = 0; r < MENU_ROWS; r++) {
          pcd8544_set_cursor(0, (uint8_t)r);
          pcd8544_print(paint_menu[r]);
        }
        if (menu_invert_row < MENU_ROWS) {
          pcd8544_invert_row(menu_invert_row);
        }
        sb1_meter_link_runes_if(bt_peer_connected, wifi_sta_connected);
        pcd8544_display();
        memcpy(last_menu, paint_menu, sizeof(last_menu));
        last_menu_invert = menu_invert_row;
        if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          sh->menu_dirty = false;
          xSemaphoreGive(sh->mutex);
        }
        strncpy(last_line4, line4, LINE_LEN - 1);
        last_line4[LINE_LEN - 1] = '\0';
        strncpy(last_line5, line5, LINE_LEN - 1);
        last_line5[LINE_LEN - 1] = '\0';
        last_pot = pot;
        last_pot_cc = pot_cc;
        s_last_bt_icon_state = bt_peer_connected;
        s_last_links_up = links_up;
        if (usb_msc_mounting && !usb_msc_medium_ready) {
          s_msc_mount_flash = mount_flash_phase ? 1u : 0u;
        } else {
          s_msc_mount_flash = 0xFFu;
        }
      }
      was_menu_active = true;
    } else {
      static bool s_live_usb = true;
      static uint8_t s_live_midi = 0xFFu;
      static bool s_live_links = false;
      static int s_live_bl = 1;
      static uint8_t s_live_pot_cc = 0xFFu;

      if (live_mode_active) {
        bool bl_pressed = (bl_button == 0);
        static uint32_t s_live_ble_rx_snap = 0xFFFFFFFFu;
        static uint32_t s_live_ble_qtick = 0;
        uint32_t ble_q = now_ms / 400u;
        bool need_live = was_menu_active || (usb_mounted != s_live_usb) || (midi_btn != s_live_midi) ||
                         (links_up != s_live_links) || (bt_peer_connected != s_last_bt_icon_state) ||
                         (bl_button != s_live_bl) || (pot_cc != s_live_pot_cc);
        if (ble_rx_last_ms != s_live_ble_rx_snap) {
          need_live = true;
        }
        if (ble_rx_last_ms != 0u && ble_q != s_live_ble_qtick) {
          s_live_ble_qtick = ble_q;
          need_live = true;
        }
        if (need_live) {
          sb1_draw_live_mode_screen(usb_mounted, midi_btn, bl_pressed, pot_cc, ble_rx_summary, ble_rx_last_ms,
                                    now_ms);
          pcd8544_display();
          s_live_ble_rx_snap = ble_rx_last_ms;
          s_live_usb = usb_mounted;
          s_live_midi = midi_btn;
          s_live_links = links_up;
          s_live_bl = bl_button;
          s_live_pot_cc = pot_cc;
          s_last_bt_icon_state = bt_peer_connected;
          s_last_links_up = links_up;
        }
        was_menu_active = false;
        continue;
      }
      s_live_usb = true;
      s_live_midi = 0xFFu;
      s_live_links = false;
      s_live_bl = 1;
      s_live_pot_cc = 0xFFu;

      const char *activity_line = line5;
      if (ble_rx_last_ms != 0u && now_ms >= ble_rx_last_ms &&
          (now_ms - ble_rx_last_ms) <= (uint32_t)SB1_BLE_RX_ACTIVITY_MS && ble_rx_summary[0] != '\0') {
        activity_line = ble_rx_summary;
      } else if (ble_rx_last_ms != 0u) {
        activity_line = "RX IDLE";
      }
      bool need = strcmp(line4, last_line4) != 0 || strcmp(activity_line, last_line5) != 0 || pot != last_pot ||
                  pot_cc != last_pot_cc || was_menu_active || midi_btn != last_midi_btn ||
                  (bt_peer_connected != s_last_bt_icon_state) || (links_up != s_last_links_up);
      if (need) {
        strncpy(last_line4, line4, LINE_LEN - 1);
        last_line4[LINE_LEN - 1] = '\0';
        strncpy(last_line5, activity_line, LINE_LEN - 1);
        last_line5[LINE_LEN - 1] = '\0';
        last_pot = pot;
        last_pot_cc = pot_cc;
        last_midi_btn = midi_btn;

        sb1_background(links_up);
        pcd8544_set_cursor(0, 0);
        pcd8544_print(potline);
        pcd8544_set_cursor(0, 1);
        pcd8544_print(btnline);
        sb1_draw_wrapped_one_line(2, line4);
        sb1_draw_wrapped_one_line(3, activity_line);
        sb1_meter_link_runes_if(bt_peer_connected, wifi_sta_connected);
        pcd8544_display();
        s_last_bt_icon_state = bt_peer_connected;
        s_last_links_up = links_up;
      }
      was_menu_active = false;
    }

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_POLL_MS));
  }
}

void display_task_create(shared_state_t *shared) {
  BaseType_t ok =
      xTaskCreate(display_task_fn, "display", DISPLAY_TASK_STACK_SIZE, shared, DISPLAY_TASK_PRIORITY, NULL);
  if (ok != pdPASS) {
    /* Heap too small for stack, or out of task control blocks — LCD task never runs → blank screen. */
    for (;;) {
      tight_loop_contents();
    }
  }
}
