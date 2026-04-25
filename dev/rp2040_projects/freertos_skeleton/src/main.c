/*
 * test_sketch_usb_midi_button.ino port — FreeRTOS on RP2040.
 * Buttons: GPIO2–5 — BL select (UI), BR/TL/TR MIDI test notes.
 * UI task -> discrete blue LEDs (BL=7, BR=12, TL=8, TR=9). Display -> Nokia 5110.
 * Build: ./build_uf2.sh
 */
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "config.h"
#include "sb1_setup.h"
#include "sb1_link_task.h"
#include "gpio_led.h"
#include "midi_task.h"
#include "ui_task.h"
#include "display_task.h"
#include "pot_task.h"
#include "usb_task.h"
#include "tusb.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

static int s_button_state = 1;
static SemaphoreHandle_t s_mutex = NULL;
static shared_state_t s_shared;

/* RP2040 `pico_enable_stdio_uart()` uses board defaults for UART0:
 * - TX: GPIO0 (idle high)
 * - RX: GPIO1
 *
 * If ESP32-C3 is unpowered, driving TX can backfeed through clamp diodes.
 * We keep TX in high-Z until we believe ESP32 is powered (see uart_stdio_bringup_task). */
#define RP2040_UART_TX_GPIO 0
#define RP2040_UART_RX_GPIO 1

/* printf / UART telemetry only after stdio_init_all() runs here (no USB CDC stdio in this build). */

/* Poll for ESP32 TX idle-high before stdio_init_all(); ESP32 may boot slower than RP2040. */
#define UART_PARTNER_PRE_DELAY_MS   500u
#define UART_PARTNER_POLL_MS        200u
#define UART_PARTNER_TIMEOUT_MS     30000u
#define UART_BRINGUP_TASK_PRIORITY  6
#define UART_BRINGUP_STACK_WORDS    256u

/* One probe burst: call only from a FreeRTOS task (uses vTaskDelay). */
static bool esp32_looks_powered(void) {
  /* ESP32 UART TX -> RP2040 RX. Idle high when partner drives the line.
   * Internal pull-down: when ESP32 is off/unwired, avoid reading floating-high as "powered". */
  gpio_init(RP2040_UART_RX_GPIO);
  gpio_set_dir(RP2040_UART_RX_GPIO, GPIO_IN);
  gpio_pull_down(RP2040_UART_RX_GPIO);

  gpio_init(RP2040_UART_TX_GPIO);
  gpio_set_dir(RP2040_UART_TX_GPIO, GPIO_IN);
  gpio_disable_pulls(RP2040_UART_TX_GPIO);

  uint32_t consecutive_high = 0;
  const uint32_t samples = 120;
  const uint32_t need_high = 8;
  const uint32_t sample_delay_ms = 2;
  for (uint32_t i = 0; i < samples; i++) {
    if (gpio_get(RP2040_UART_RX_GPIO)) {
      consecutive_high++;
      if (consecutive_high >= need_high) {
        return true;
      }
    } else {
      consecutive_high = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(sample_delay_ms));
  }
  return false;
}

static void uart_stdio_bringup_task(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;
  vTaskDelay(pdMS_TO_TICKS(UART_PARTNER_PRE_DELAY_MS));
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(UART_PARTNER_TIMEOUT_MS);
  while (xTaskGetTickCount() < deadline) {
    if (esp32_looks_powered()) {
      stdio_init_all();
      sb1_set_stdio_ready(true);
      if (sh) {
        sb1_link_task_create(sh);
      }
      vTaskDelete(NULL);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(UART_PARTNER_POLL_MS));
  }
  vTaskDelete(NULL);
}

static void buttons_hw_init(void) {
  const uint pins[BTN_PANEL_COUNT] = {
      BTN_SELECT_GPIO,
      BTN_MIDI_A_GPIO,
      BTN_MIDI_B_GPIO,
      BTN_MIDI_C_GPIO,
  };
  for (unsigned i = 0; i < BTN_PANEL_COUNT; i++) {
    gpio_init(pins[i]);
    gpio_set_dir(pins[i], GPIO_IN);
    gpio_pull_up(pins[i]);
  }
}

static void startup_task_fn(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;
  vTaskDelay(pdMS_TO_TICKS(1500));
  if (sh && sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    bool mounted = tud_mounted();
    sh->usb_mounted = mounted;
    if (!sh->menu_active) {
      if (mounted) {
        snprintf(sh->line4, LINE_LEN, "MIDI READY");
        snprintf(sh->line5, LINE_LEN, "BUTTON TEST");
      } else {
        snprintf(sh->line4, LINE_LEN, "NOT MOUNTED");
        snprintf(sh->line5, LINE_LEN, "CHECK USB STACK");
      }
    }
    xSemaphoreGive(sh->mutex);
  }
  vTaskDelete(NULL);
}

int main(void) {
  sb1_set_stdio_ready(false);

  gpio_led_init();
  buttons_hw_init();

  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    for (;;) tight_loop_contents();
  }
  s_shared.button_state = &s_button_state;
  s_shared.pot_step = POT_STEP_MAX / 2u;
  s_shared.pot_cc_127 = s_shared.pot_step;
  s_shared.pot_raw = (uint16_t)(((uint32_t)s_shared.pot_step * 4095u) / POT_STEP_MAX);
  s_shared.pot_filtered_raw = s_shared.pot_raw;
  s_shared.pot_quant_step =
      (uint8_t)((uint32_t)s_shared.pot_step * (POT_QUANT_LEVELS - 1u) / POT_STEP_MAX);
  s_shared.ui_rotate_events = 0;
  s_shared.ui_rotate_events_last_sec = 0;
  s_shared.mutex = s_mutex;
  s_shared.usb_mounted = false;
  s_shared.menu_active = true;
  s_shared.live_mode_active = false;
  s_shared.menu_dirty = false;
  s_shared.menu_sel = 0;
  s_shared.menu_invert_row = 0xFFu;
  s_shared.midi_channel = 1;
  s_shared.program_number = 0;
  s_shared.cc_number = POT_MIDI_CC;
  s_shared.tap_bpm = 120;
  s_shared.arp_enabled = false;
  s_shared.arp_rate = 8;
  s_shared.program_change_pending = false;
  s_shared.midi_btn_live = 0;
  s_shared.ui_setup_hold_active = false;
  s_shared.bt_pairing_active = false;
  s_shared.connectivity_screen = SB1_CONN_SCREEN_ROOT;
  s_shared.connectivity_sel = 0;
  s_shared.connectivity_qr_visible = false;
  s_shared.connectivity_qr_wifi = false;
  s_shared.connectivity_connecting_bt = false;
  s_shared.connectivity_connecting_wifi = false;
  s_shared.bt_pairing_dirty = false;
  s_shared.bt_peer_connected = false;
  s_shared.bt_peer_name[0] = '\0';
  s_shared.wifi_sta_connected = false;
  s_shared.ble_midi_sink = SB1_BLE_MIDI_SINK_MERGE;
  s_shared.osc_enabled = false;
  s_shared.menu_view = SB1_MENU_VIEW_LIST;
  s_shared.fw_ota_step = 0;
  s_shared.about_line_sel = 0;
  s_shared.about_detail_open = false;
  s_shared.about_detail_text[0] = '\0';
  s_shared.menu_esc_available = false;
  s_shared.menu_parent_preview = false;
  s_shared.menu_verticalmenu_enable = false;
  s_shared.menu_vm_count = 0;
  s_shared.auto_shutdown_minutes = 0;
  memset(s_shared.menu_line, 0, sizeof(s_shared.menu_line));
  strncpy(s_shared.last_event, "---", LAST_EVENT_LEN - 1);
  s_shared.last_event[LAST_EVENT_LEN - 1] = '\0';
  snprintf(s_shared.line4, LINE_LEN, "USB MIDI INIT");
  snprintf(s_shared.line5, LINE_LEN, "WAIT PC ENUM");

  usb_task_create();
  xTaskCreate(uart_stdio_bringup_task, "uart_bringup", UART_BRINGUP_STACK_WORDS, &s_shared,
              UART_BRINGUP_TASK_PRIORITY, NULL);
  ui_task_create(&s_shared);
  pot_task_create(&s_shared);
  display_task_create(&s_shared);

  TaskHandle_t midi_handle = midi_task_create(&s_shared);
  if (midi_handle == NULL) {
    for (;;) tight_loop_contents();
  }

  xTaskCreate(startup_task_fn, "startup", 128, &s_shared, 1, NULL);

  vTaskStartScheduler();

  for (;;) tight_loop_contents();
  return 0;
}
