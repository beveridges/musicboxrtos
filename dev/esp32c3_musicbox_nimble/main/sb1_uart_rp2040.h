/*
 * UART status lines to RP2040 (same UART as SB1BT). Implemented in main.c when MUSICBOX_UART_BRIDGE.
 */
#pragma once

#include <stdbool.h>

/** STA has IP / not — mirrors SB1BT line discipline: SB1WF,0 / SB1WF,1 */
void sb1_uart_send_wifi_status(bool sta_connected);
