/*
 * SB1 setup / provisioning hook — RP2040 has no Wi-Fi; stub logs for bring-up.
 */
#include "sb1_setup.h"
#include <stdio.h>

static bool s_stdio_ready = false;

void sb1_set_stdio_ready(bool ready) {
  s_stdio_ready = ready;
}

bool sb1_is_stdio_ready(void) {
  return s_stdio_ready;
}

void sb1_enter_setup_mode(void) {
  if (!s_stdio_ready) {
    return;
  }
  printf("SB1: setup mode entered (stub — no SoftAP on RP2040; use ESP32-C3 for Wi-Fi provisioning).\n");
}
