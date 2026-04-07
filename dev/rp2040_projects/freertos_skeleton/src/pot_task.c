/*
 * Poll GPIO29 / ADC3 — B10K wiper. Light averaging smooths scratchy tracks a bit.
 * When USB MIDI is mounted, sends CC (POT_MIDI_CC) 0..127 when the value changes.
 */
#include "config.h"
#include "pot_task.h"
#include "hardware/adc.h"
#include "tusb.h"
#include "FreeRTOS.h"
#include "task.h"

static void pot_task_fn(void *pvParameters) {
  shared_state_t *sh = (shared_state_t *)pvParameters;
  static uint8_t s_last_cc_sent = 0xFFu;
  static uint32_t s_ema = 0;
  uint8_t quant_step = 0;
  bool quant_init = false;
  TickType_t calib_start_tick = xTaskGetTickCount();

  adc_init();
  adc_gpio_init(POT_PIN_GPIO);

  for (;;) {
    uint32_t sum = 0;
    for (int i = 0; i < 4; i++) {
      adc_select_input(POT_ADC_CHANNEL);
      sum += adc_read();
    }
    uint16_t avg = (uint16_t)(sum / 4u);
    if (s_ema == 0) {
      s_ema = avg;
    } else {
      /* Use a signed delta so the EMA can move down without unsigned underflow. */
      int32_t delta = (int32_t)avg - (int32_t)s_ema;
      s_ema = (uint32_t)((int32_t)s_ema + (delta >> POT_FILTER_SHIFT));
    }
    uint16_t filt = (uint16_t)s_ema;
    uint8_t cc = (uint8_t)((filt * 127u + 2048u) / 4095u);
    uint8_t step = cc;
    uint16_t bin_w = (uint16_t)(4096u / POT_QUANT_LEVELS);
    if (bin_w == 0) {
      bin_w = 1;
    }
    uint16_t center = (uint16_t)quant_step * bin_w + (bin_w / 2u);
    uint16_t low = (center > POT_HYST_RAW) ? (uint16_t)(center - POT_HYST_RAW) : 0u;
    uint16_t high = center + POT_HYST_RAW;
    if (high > 4095u) {
      high = 4095u;
    }

    TickType_t now_tick = xTaskGetTickCount();
    bool in_calibration = (now_tick - calib_start_tick) < pdMS_TO_TICKS(POT_CALIBRATION_MS);
    if (!quant_init || in_calibration) {
      uint8_t init_q = (uint8_t)(((uint32_t)filt * POT_QUANT_LEVELS) / 4096u);
      if (init_q >= POT_QUANT_LEVELS) {
        init_q = POT_QUANT_LEVELS - 1u;
      }
      quant_step = init_q;
      quant_init = true;
    } else {
      while (quant_step + 1u < POT_QUANT_LEVELS && filt > high) {
        quant_step++;
        center = (uint16_t)quant_step * bin_w + (bin_w / 2u);
        low = (center > POT_HYST_RAW) ? (uint16_t)(center - POT_HYST_RAW) : 0u;
        high = center + POT_HYST_RAW;
        if (high > 4095u) {
          high = 4095u;
        }
      }
      while (quant_step > 0u && filt < low) {
        quant_step--;
        center = (uint16_t)quant_step * bin_w + (bin_w / 2u);
        low = (center > POT_HYST_RAW) ? (uint16_t)(center - POT_HYST_RAW) : 0u;
        high = center + POT_HYST_RAW;
        if (high > 4095u) {
          high = 4095u;
        }
      }
    }
    step = quant_step;
    bool menu_active = false;
    bool bt_pairing = false;

    if (sh && sh->mutex) {
      if (xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        sh->pot_raw = avg;
        sh->pot_filtered_raw = filt;
        sh->pot_cc_127 = cc;
        sh->pot_step = step;
        sh->pot_quant_step = quant_step;
        menu_active = sh->menu_active;
        bt_pairing = sh->bt_pairing_active;
        xSemaphoreGive(sh->mutex);
      }
    }

#if CFG_TUD_MIDI
    uint8_t cc_quant = 0;
    if (POT_QUANT_LEVELS > 1u) {
      cc_quant = (uint8_t)((uint32_t)quant_step * 127u / (uint32_t)(POT_QUANT_LEVELS - 1u));
    }

    if (!tud_mounted()) {
      s_last_cc_sent = 0xFFu;
    } else if (!menu_active && !bt_pairing && cc_quant != s_last_cc_sent) {
      uint8_t msg[3] = {
          (uint8_t)(0xB0u | (uint8_t)(MIDI_CH - 1u)),
          (uint8_t)POT_MIDI_CC,
          cc_quant,
      };
      if (tud_midi_stream_write(0, msg, 3) > 0) {
        s_last_cc_sent = cc_quant;
      }
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(POT_POLL_MS));
  }
}

void pot_task_create(shared_state_t *shared) {
  xTaskCreate(pot_task_fn, "pot", POT_TASK_STACK_SIZE, shared, POT_TASK_PRIORITY, NULL);
}
