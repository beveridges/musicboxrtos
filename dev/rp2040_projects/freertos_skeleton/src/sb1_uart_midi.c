#include "sb1_uart_midi.h"
#include "sb1_setup.h"
#include "hardware/uart.h"
#include "FreeRTOS.h"
#include "semphr.h"

void sb1_uart_mirror_midi(const uint8_t *data, size_t len, shared_state_t *sh) {
  if (!sb1_is_stdio_ready() || !data || len == 0 || !sh) {
    return;
  }
  bool peer = false;
  if (sh->mutex && xSemaphoreTake(sh->mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
    peer = sh->bt_peer_connected;
    xSemaphoreGive(sh->mutex);
  }
  if (!peer) {
    return;
  }
  for (size_t i = 0; i < len; i++) {
    uart_putc(uart0, (char)data[i]);
  }
}
