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
#define DISPLAY_TASK_STACK_SIZE 256
#define POT_TASK_STACK_SIZE     256

#define LAST_EVENT_LEN 24
#define LINE_LEN       20
/* Nokia 5110: 84px / 6px per char */
#define DISPLAY_COLS   14
#define MENU_ROWS      6

#define MENU_LONG_PRESS_MS 600
/* Long-hold setup (Select): 1s per third (3s total), TL / +TR / +BR; then 1s all-LED flash. */
#define SETUP_HOLD_TOTAL_MS    3000u
#define SETUP_HOLD_PHASE1_MS   (SETUP_HOLD_TOTAL_MS / 3u)
#define SETUP_HOLD_PHASE2_MS   ((SETUP_HOLD_TOTAL_MS * 2u) / 3u)
#define SETUP_PHASE3_SHOW_MS   50u
#define SETUP_SUCCESS_FLASH_MS 1000u
#define UI_EVENT_QUEUE_LEN 8
#define POT_STEP_MAX 127
#define POT_FILTER_SHIFT 2
#define POT_CALIBRATION_MS 500
#define POT_QUANT_BITS 4            /* 0..15 coarse bins for stronger stability */
#define POT_QUANT_LEVELS (1u << POT_QUANT_BITS)
#define POT_HYST_RAW 36             /* ADC hysteresis for Schmitt edges */
#define UI_ROTATE_MIN_EVENT_MS 60   /* rate limit for rotate events */
#define UI_TELEMETRY_PRINT_MS 500

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
  char last_event[LAST_EVENT_LEN];
  char line4[LINE_LEN];
  char line5[LINE_LEN];
  bool menu_active;
  bool menu_dirty;
  uint8_t menu_sel; /* selected row index on current screen (debug / UI) */
  uint8_t menu_invert_row; /* 0..MENU_ROWS-1 row to draw inverted, or 0xFF for none */
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
  SemaphoreHandle_t mutex;
} shared_state_t;

#endif /* CONFIG_H */
