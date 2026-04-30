/*
 * config.h — Pins and constants (Waveshare RP2040 Zero).
 * Nokia 5110, discrete blue LEDs, buttons, USB MIDI.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "FreeRTOS.h"
#include "semphr.h"
#include <stdbool.h>

/* Nokia 5110 (PCD8544) */
#define PIN_SCLK  10
#define PIN_DIN   11
#define PIN_DC    14
#define PIN_CS    13
#define PIN_RST   15

/* Discrete blue LEDs — GPIO -> resistor -> LED; active-high = ON (not WS2812).
 * All four are always initialized and driven (BL in ui_task; BR/TL/TR in midi_task).
 *
 * RP2040-Zero: onboard RGB is WS2812 DIN on GPIO16. BR uses GPIO12 (not on PCD8544 SPI here). */
#define LED_BLUE_SELECT_GPIO  7   /* BL — follows select button (GPIO2) */
#define LED_BLUE_MIDI_A_GPIO  12  /* BR — MIDI A button (GPIO3) */
#define LED_BLUE_MIDI_B_GPIO  8   /* TL — MIDI B button (GPIO4) */
#define LED_BLUE_MIDI_C_GPIO  9   /* TR — MIDI C button (GPIO5) */
#define LED_ACTIVE_LEVEL      1
#define LED_INACTIVE_LEVEL    (1 - (LED_ACTIVE_LEVEL))

/* Four panel buttons: active-low to GND, internal pull-up (see buttons_hw_init in main.c).
 * If TL works on the LCD as GPIO5 and TR as GPIO4, swap BTN_MIDI_B_GPIO and BTN_MIDI_C_GPIO. */
#define BTN_SELECT_GPIO  2  /* BL: menu select (short/long) */
#define BTN_MIDI_A_GPIO  3  /* BR: MIDI note A (GPIO3) */
#define BTN_MIDI_B_GPIO  4  /* TL: MIDI note B (GPIO4) */
#define BTN_MIDI_C_GPIO  5  /* TR: MIDI note C (GPIO5) */
#define BTN_PANEL_COUNT   4u

/* Potentiometer — GPIO 29 is ADC3 (wiper to pin; one end 3V3, other GND) */
#define POT_PIN_GPIO     29
#define POT_ADC_CHANNEL  3
#define POT_POLL_MS      50

/* MIDI */
#define MIDI_CH    1
#define MIDI_NOTE  60
#define MIDI_VEL   100
#define BTN_MIDI_NOTE_A  (MIDI_NOTE + 0) /* C major triad on test buttons */
#define BTN_MIDI_NOTE_B  (MIDI_NOTE + 4)
#define BTN_MIDI_NOTE_C  (MIDI_NOTE + 7)
/* Pot -> USB MIDI CC (0–127). CC16 = general purpose; try CC1 (mod wheel) or CC74 (filter) if you prefer. */
#define POT_MIDI_CC  16

/* Timing */
#define DEBOUNCE_MS      20
#define UI_POLL_MS       50
#define DISPLAY_POLL_MS  200

/* Task priorities (0–7) */
#define MIDI_TASK_PRIORITY    5
#define UI_TASK_PRIORITY      2
#define DISPLAY_TASK_PRIORITY 1
#define POT_TASK_PRIORITY       1

#define MIDI_TASK_STACK_SIZE    256
#define UI_TASK_STACK_SIZE      512
/* QR encode uses some stack; 4 KiB is enough for Nayuki qrcodegen here. Keep modest so xTaskCreate
 * succeeds within configTOTAL_HEAP_SIZE (large stacks can silently fail task creation → blank LCD). */
#define DISPLAY_TASK_STACK_SIZE 4096
#define POT_TASK_STACK_SIZE     256

#define LAST_EVENT_LEN 24
#define LINE_LEN       20
/* Nokia 5110: monospace column budget for snprintf (8px prop font; ~14 cols safe). */
#define DISPLAY_COLS   14
/** List / ABOUT body text width when verticalmenu strip is shown (narrower than full DISPLAY_COLS). */
#define DISPLAY_COLS_VM 12
/** Pixel X of verticalmenu track (right edge of 84×48 display). */
#define SB1_VERTMENU_TRACK_X 83u
/* Text rows on PCD8544 list UI (title + items; max matches PCD8544_TEXT_LINES). */
#define MENU_ROWS      6

#define MENU_LONG_PRESS_MS 600
/* TR (MIDI C) hold -> reset_usb_boot (UF2); separate from menu long-press. */
#define TR_USB_BOOT_HOLD_MS 1500u
/* TL (MIDI B) hold -> USB MSC “SB1STORAGE” mode (same tier as TR UF2; not menu 600 ms). */
#define TL_USB_MSC_HOLD_MS TR_USB_BOOT_HOLD_MS
/* Bluetooth pairing LED choreography (see manual/SB1-Bluetooth-LED-Gestures-Plan.md). */
#define PAIR_LED_FIRST_MS       1000u /* First dwell (longer than MENU_LONG_PRESS_MS). */
#define PAIR_LED_STEP_MS        500u
/* Entry: dark 1s, then TL→+TR→+BR→+BL each 0.5s, then all off at 3.0s → pairing UI. */
#define PAIR_ENTRY_ALL_OFF_MS   (PAIR_LED_FIRST_MS + 5u * PAIR_LED_STEP_MS) /* 3000 */
/* Exit: all on 1s, then peel BL→BR→TR→TL each 0.5s, all off at 2.5s → leave pairing. */
#define PAIR_EXIT_ALL_OFF_MS    (PAIR_LED_FIRST_MS + 3u * PAIR_LED_STEP_MS) /* 2500 */
#define UI_EVENT_QUEUE_LEN 8
#define POT_STEP_MAX 127
/* Legacy names retained for compatibility with menu/display shared-state fields. */
#define POT_FILTER_SHIFT 2
#define POT_CALIBRATION_MS 500
#define POT_QUANT_BITS 4
#define POT_QUANT_LEVELS (1u << POT_QUANT_BITS)
#define POT_HYST_RAW 36
#define UI_ROTATE_MIN_EVENT_MS 60   /* rate limit for rotate events */
#define UI_TELEMETRY_PRINT_MS 500

/* BLE / pairing UI — must match default ESP32 `device_name` when unset (see esp32 main.c). */
#define SB1_BT_DEVICE_NAME "SB1"
/* QR payload (UTF-8 ASCII); keep reasonably short for small modules on 84x48 LCD. */
#define SB1_BT_QR_PAYLOAD SB1_BT_DEVICE_NAME
/* Wi‑Fi config AP QR (matches ESP32 SoftAP SSID in wifi_sb1.c, open network). */
#define SB1_WIFI_QR_PAYLOAD "WIFI:S:SB1-Config;T:nopass;;"
/* Connectivity setup UI: screen id (after hold-to-enter gesture). */
#define SB1_CONN_SCREEN_ROOT 0u
#define SB1_CONN_SCREEN_BT   1u
#define SB1_CONN_SCREEN_WIFI 2u
#define SB1_CONN_SCREEN_BT_DEV 3u
#define SB1_CONN_SCREEN_WIFI_SCAN 4u
#define SB1_CONN_CONNECTING_FLASH_MS 400u
#define SB1_WIFI_SCAN_MAX_RESULTS 8u
#define SB1_WIFI_SSID_MAX 32u
/** Dashboard / live mode: show last BLE MIDI summary for this long (ms), then "RX IDLE". */
#define SB1_BLE_RX_ACTIVITY_MS 1500u
/* Connected pairing: peer/device name on its own line, max glyphs shown (84px / prop font). */
#define SB1_BT_PAIR_PEER_DISPLAY_LEN 11u
/* ESP32 → RP2040 UART lines: SB1BT,0 / SB1BT,1,<name> — name also shown truncated on pairing LCD. */
#define BT_PEER_NAME_MAX ((DISPLAY_COLS * 2u) + 1u)

/* Build / version metadata (About screen). */
#define SB1_BUILD_GITREVHEAD           "7deb2cf"
#define SB1_BUILD_BUILDNUMBER          "26.02-alpha.01.09"
#define SB1_BUILD_VERSIONNUMBER        "26.02-alpha.01"
#define SB1_BUILD_FRIENDLYVERSIONNAME  "SB1.MIDI.DEVICE"
#define SB1_BUILD_VERSIONNAME          "SB1.MIDI.DEVICE.26.02-alpha.01"
#define SB1_BUILD_GITTAG                 "snapshot-2026-02-17"
#define SB1_BUILD_CONDAENVIRONMENTNAME "na"
#define SB1_BUILD_PYTHONVERSION        "3.11.13"
#define SB1_BUILD_CONDAENVFILE         "na.yml"
#define SB1_BUILD_RELEASENAME          "UNKNOWN_RELEASE"

/* Must match `sb1_about_lines[]` count in display_task.c (build metadata rows). */
#define SB1_ABOUT_INFO_LINE_COUNT 10u
/* Full line text for About detail modal (long version strings). */
#define SB1_ABOUT_DETAIL_BUF 48u

/* Legacy short strings (not shown on About LCD; About lists SB1_BUILD_* only — see display_task). */
#define SB1_ABOUT_DISP_0 "7deb2cf"
#define SB1_ABOUT_DISP_1 "26.02.a.01.09"
#define SB1_ABOUT_DISP_2 "SB1.DEVICE"
#define SB1_ABOUT_DISP_3 "sshot-2026-02"
#define SB1_ABOUT_DISP_4 "naenvironment"
#define SB1_ABOUT_DISP_5 "3.11.13"
#define SB1_ABOUT_DISP_6 "naenv.yml"
#define SB1_ABOUT_DISP_7 "sesquialtera"

/** Max 8.3 names listed under UTILITY → MIDI FILES → FILES (RAM disk root scan). */
#define SB1_MSC_FILE_LIST_MAX 16u

/* Shared state: MIDI task writes; UI and display tasks read under mutex. */
typedef struct {
  int *button_state;
  uint16_t pot_raw;   /* 12-bit ADC 0..4095 */
  uint8_t pot_cc_127; /* same knob, mapped for MIDI / UI */
  uint16_t pot_filtered_raw;
  uint8_t pot_step;
  uint8_t pot_quant_step; /* 0..(POT_QUANT_LEVELS-1) */
  uint32_t ui_rotate_events;
  uint16_t ui_rotate_events_last_sec;
  bool usb_mounted;
  /** TL long-hold: usb_task arms MSC medium + LCD “SB1STORAGE / ATTACHED” (0/1). */
  volatile uint8_t usb_msc_tl_request;
  /** True while menu-initiated mount is in progress (used for "MOUNTING" blink feedback). */
  bool usb_msc_mounting;
  /** True while host may access MSC LUN (after TL gesture until host eject). */
  bool usb_msc_medium_ready;
  /** Full-screen MSC attached message until this time (ms since boot); 0 = off. */
  uint32_t usb_msc_attached_until_ms;
  /** Host issued START/STOP eject; lun not ready until next TL gesture. */
  bool usb_msc_host_ejected;
  char msc_file_list[SB1_MSC_FILE_LIST_MAX][13];
  uint8_t msc_file_list_count;
  char last_event[LAST_EVENT_LEN];
  char line4[LINE_LEN];
  char line5[LINE_LEN];
  bool menu_active;
  /** Dashboard: three-line LIVE MODE layout (see display_task); cleared when menu opens. */
  bool live_mode_active;
  bool menu_dirty;
  uint8_t menu_sel; /* selected row index on current screen (debug / UI) */
  uint8_t menu_invert_row; /* 0..MENU_ROWS-1 → pcd8544_invert_row (list UI keeps 0xFF; XOR + sparse font = bars) */
  char menu_line[MENU_ROWS][LINE_LEN];
  uint8_t midi_channel; /* 1..16 */
  uint8_t program_number; /* 0..127 */
  uint8_t cc_number; /* 0..127 */
  uint16_t tap_bpm;
  bool arp_enabled;
  uint8_t arp_rate;
  bool program_change_pending;
  /* Live mode: raw pressed=low, bits 0,1,2 = MIDI A,B,C on GPIO3,4,5 (see midi_task). */
  uint8_t midi_btn_live;
  /* When true, ui_task drives TL/TR/BR for setup gesture; midi_task must not override. */
  bool ui_setup_hold_active;
  /* After BL long-hold setup gesture completes: connectivity UI on the LCD. */
  bool bt_pairing_active;
  uint8_t connectivity_screen; /* SB1_CONN_SCREEN_* */
  uint8_t connectivity_sel;    /* selected row index on current screen */
  bool connectivity_qr_visible;
  bool connectivity_qr_wifi; /* QR payload: false = BLE name, true = Wi‑Fi join string */
  bool connectivity_connecting_bt;
  bool connectivity_connecting_wifi;
  bool connectivity_wifi_scanning;
  uint8_t connectivity_wifi_scan_count;
  uint8_t connectivity_wifi_scan_sel;
  char connectivity_wifi_ssids[SB1_WIFI_SCAN_MAX_RESULTS][SB1_WIFI_SSID_MAX + 1u];
  char connectivity_wifi_status[LINE_LEN];
  bool bt_pairing_dirty;
  /* From ESP32 UART (SB1BT,...); used in pairing text screen when bt_pairing_active. */
  bool bt_peer_connected;
  char bt_peer_name[BT_PEER_NAME_MAX];
  /** Monotonic count of BT connect sessions observed from SB1BT,1 events. */
  uint32_t bt_session_count;
  /** ms-since-boot timestamp of last SB1BT connect/disconnect state change. */
  uint32_t bt_last_change_ms;
  /** ESP32 BLE telemetry: advertise state (SB1BLE,ADV). */
  bool ble_adv_active;
  /** ESP32 BLE telemetry: BOOTING/ADVERTISING/CONNECTED/RECOVERING/FAULT. */
  uint8_t ble_state;
  /** ESP32 BLE telemetry: last GAP/advertise error code. */
  int16_t ble_last_err;
  /** ESP32 BLE telemetry: last disconnect reason code. */
  int16_t ble_last_disc_reason;
  /** ESP32 BLE telemetry: recovery loop counter. */
  uint32_t ble_recovery_count;
  /** ESP32 link protocol version reported by SB1BLE,PROTO,<n>. */
  uint8_t ble_proto_version;
  /** RP2040 one-shot guard: startup READV already issued to ESP32. */
  bool ble_startup_readv_sent;
  /** Until when (ms-since-boot) BT edge toast should be shown in pairing UI; 0=off. */
  uint32_t bt_toast_until_ms;
  /** 1 = connected toast, 2 = disconnected toast. */
  uint8_t bt_toast_kind;
  /* From ESP32 UART: SB1WF,0 / SB1WF,1 — STA associated with IP. */
  bool wifi_sta_connected;
  /** 0=USB only, 1=merge with local, 2=on-device (no USB echo). */
  uint8_t ble_midi_sink;
  /** Total BLE MIDI packets received from ESP32 framed UART path. */
  uint32_t ble_rx_packets_total;
  /** Last BLE MIDI receive time (ms-since-boot); 0 means none yet. */
  uint32_t ble_rx_last_ms;
  /** Last BLE MIDI packet summary (compact, display-safe). */
  char ble_rx_last_summary[LINE_LEN];
  /** Increments when a BLE packet cannot be summarized cleanly for UI. */
  uint32_t ble_rx_overrun_count;
  bool osc_enabled; /* NI: future OSC bridge */
  /** LIST = normal menu rows; SYSTEM_ABOUT / SYSTEM_FW = full-screen layouts (see display_task). */
  uint8_t menu_view;
  /** FIRMWARE flow: 0 = main FW screen; 1 = "ARE YOU SURE?" (OTA confirm stub). */
  uint8_t fw_ota_step;
  /** ABOUT: selected info row index 0..SB1_ABOUT_INFO_LINE_COUNT-1 (pot navigation). */
  uint8_t about_line_sel;
  /** ABOUT: framed full-string modal (short BL toggles). */
  bool about_detail_open;
  char about_detail_text[SB1_ABOUT_DETAIL_BUF];
  /** 0=NEVER, 5, 15, 30, 60, 120 minutes — idle auto-off (UI only until power policy exists). */
  uint8_t auto_shutdown_minutes;
  /** True after menu_render: long-press BL would go to parent (not on root list). */
  bool menu_esc_available;
  /** True while BL held past long-press in submenu: LCD shows parent list only (FSM unchanged). */
  bool menu_parent_preview;
  /** Nokia-style scroll column (ABOUT list); see sb1_verticalmenu.c */
  bool menu_verticalmenu_enable;
  uint8_t menu_vm_count; /* items in scroll domain; thumb maps sel in [0, count-1] */
  SemaphoreHandle_t mutex;
} shared_state_t;

#define SB1_MENU_VIEW_LIST            0u
#define SB1_MENU_VIEW_SYSTEM_ABOUT    1u
#define SB1_MENU_VIEW_SYSTEM_FW       2u
#define SB1_MENU_VIEW_SYSTEM_FW_OTA   3u
#define SB1_MENU_VIEW_SYSTEM_FW_SURE  4u
#define SB1_MENU_VIEW_SYSTEM_ABOUT_DETAIL 5u
#define SB1_MENU_VIEW_TAP_TEMPO       6u
#define SB1_MENU_VIEW_USB_MSC_ATTACHED 7u

/** Where inbound BLE MIDI (from ESP) is routed on RP2040. */
#define SB1_BLE_MIDI_SINK_USB    0u
#define SB1_BLE_MIDI_SINK_MERGE  1u
#define SB1_BLE_MIDI_SINK_DEVICE 2u

#define SB1_BLE_STATE_BOOTING 0u
#define SB1_BLE_STATE_ADVERTISING 1u
#define SB1_BLE_STATE_CONNECTED 2u
#define SB1_BLE_STATE_RECOVERING 3u
#define SB1_BLE_STATE_FAULT 4u

/** ESP32<->RP2040 control/data link protocol version. */
#define SB1_LINK_PROTO_VERSION 1u

/** UART binary frame from ESP32 before raw MIDI payload (see sb1_link_task / esp32 main). */
#define SB1_UART_FRAME_SOH 0x01u

#endif /* CONFIG_H */
