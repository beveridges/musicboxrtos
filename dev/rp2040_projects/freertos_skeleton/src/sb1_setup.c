/*
 * SB1 setup / provisioning hook — RP2040 has no Wi-Fi; stub logs for bring-up.
 */
#include "sb1_setup.h"
#include <stdio.h>

void sb1_enter_setup_mode(void) {
  printf("SB1: setup mode entered (stub — no SoftAP on RP2040; use ESP32-C3 for Wi-Fi provisioning).\n");
}
