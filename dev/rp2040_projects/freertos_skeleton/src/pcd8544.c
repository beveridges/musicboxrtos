/*
 * Minimal PCD8544 (Nokia 5110) driver — soft SPI, 84x48 framebuffer, 5x7 font.
 */
#include "pcd8544.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <stddef.h>
#include <string.h>

static uint8_t _sclk, _din, _dc, _cs, _rst;
static uint8_t _fb[PCD8544_WIDTH * PCD8544_ROWS];
static uint8_t _cx, _cy;

#define PCD8544_CMD_FUNC_SET    0x20
#define PCD8544_CMD_EXTENDED    0x21
#define PCD8544_CMD_SET_BIAS    0x14
#define PCD8544_CMD_SET_VOP     0x80
#define PCD8544_CMD_DISP_CTL    0x08
#define PCD8544_CMD_NORMAL      0x0C
#define PCD8544_CMD_SET_X       0x80
#define PCD8544_CMD_SET_Y       0x40

static inline void _spi_byte(uint8_t byte) {
  for (int i = 7; i >= 0; i--) {
    gpio_put(_sclk, 0);
    gpio_put(_din, (byte >> i) & 1);
    busy_wait_us(1);
    gpio_put(_sclk, 1);
    busy_wait_us(1);
  }
}

static void _write_cmd(uint8_t cmd) {
  gpio_put(_dc, 0);
  gpio_put(_cs, 0);
  _spi_byte(cmd);
  gpio_put(_cs, 1);
}

static void _write_data(uint8_t data) {
  gpio_put(_dc, 1);
  gpio_put(_cs, 0);
  _spi_byte(data);
  gpio_put(_cs, 1);
}

/* 5x7 font (ASCII 0x20..0x7F), each char 5 cols + 1 space. One column = 1 byte (8 rows). */
static const uint8_t _font5x7[][5] = {
  { 0x00, 0x00, 0x00, 0x00, 0x00 }, /* space */
  { 0x00, 0x00, 0x5F, 0x00, 0x00 }, /* ! */
  { 0x00, 0x07, 0x00, 0x07, 0x00 }, /* " */
  { 0x14, 0x7F, 0x14, 0x7F, 0x14 }, /* # */
  { 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, /* $ */
  { 0x23, 0x13, 0x08, 0x64, 0x62 }, /* % */
  { 0x36, 0x49, 0x56, 0x20, 0x50 }, /* & */
  { 0x00, 0x08, 0x07, 0x03, 0x00 }, /* ' */
  { 0x00, 0x1C, 0x22, 0x41, 0x00 }, /* ( */
  { 0x00, 0x41, 0x22, 0x1C, 0x00 }, /* ) */
  { 0x2A, 0x1C, 0x7F, 0x1C, 0x2A }, /* * */
  { 0x08, 0x08, 0x3E, 0x08, 0x08 }, /* + */
  { 0x00, 0x80, 0x70, 0x30, 0x00 }, /* , */
  { 0x08, 0x08, 0x08, 0x08, 0x08 }, /* - */
  { 0x00, 0x00, 0x60, 0x60, 0x00 }, /* . */
  { 0x20, 0x10, 0x08, 0x04, 0x02 }, /* / */
  { 0x3E, 0x51, 0x49, 0x45, 0x3E }, /* 0 */
  { 0x00, 0x42, 0x7F, 0x40, 0x00 }, /* 1 */
  { 0x72, 0x49, 0x49, 0x49, 0x46 }, /* 2 */
  { 0x21, 0x41, 0x49, 0x4D, 0x33 }, /* 3 */
  { 0x18, 0x14, 0x12, 0x7F, 0x10 }, /* 4 */
  { 0x27, 0x45, 0x45, 0x45, 0x39 }, /* 5 */
  { 0x3C, 0x4A, 0x49, 0x49, 0x31 }, /* 6 */
  { 0x41, 0x21, 0x11, 0x09, 0x07 }, /* 7 */
  { 0x36, 0x49, 0x49, 0x49, 0x36 }, /* 8 */
  { 0x46, 0x49, 0x49, 0x29, 0x1E }, /* 9 */
  { 0x00, 0x36, 0x36, 0x00, 0x00 }, /* : */
  { 0x00, 0x56, 0x36, 0x00, 0x00 }, /* ; */
  { 0x08, 0x14, 0x22, 0x41, 0x00 }, /* < */
  { 0x14, 0x14, 0x14, 0x14, 0x14 }, /* = */
  { 0x00, 0x41, 0x22, 0x14, 0x08 }, /* > */
  { 0x02, 0x01, 0x59, 0x09, 0x06 }, /* ? */
  { 0x3E, 0x41, 0x5D, 0x59, 0x4E }, /* @ */
  { 0x7C, 0x12, 0x11, 0x12, 0x7C }, /* A */
  { 0x7F, 0x49, 0x49, 0x49, 0x36 }, /* B */
  { 0x3E, 0x41, 0x41, 0x41, 0x22 }, /* C */
  { 0x7F, 0x41, 0x41, 0x41, 0x3E }, /* D */
  { 0x7F, 0x49, 0x49, 0x49, 0x41 }, /* E */
  { 0x7F, 0x09, 0x09, 0x09, 0x01 }, /* F */
  { 0x3E, 0x41, 0x41, 0x51, 0x73 }, /* G */
  { 0x7F, 0x08, 0x08, 0x08, 0x7F }, /* H */
  { 0x00, 0x41, 0x7F, 0x41, 0x00 }, /* I */
  { 0x20, 0x40, 0x41, 0x3F, 0x01 }, /* J */
  { 0x7F, 0x08, 0x14, 0x22, 0x41 }, /* K */
  { 0x7F, 0x40, 0x40, 0x40, 0x40 }, /* L */
  { 0x7F, 0x02, 0x0C, 0x02, 0x7F }, /* M */
  { 0x7F, 0x04, 0x08, 0x10, 0x7F }, /* N */
  { 0x3E, 0x41, 0x41, 0x41, 0x3E }, /* O */
  { 0x7F, 0x09, 0x09, 0x09, 0x06 }, /* P */
  { 0x3E, 0x41, 0x51, 0x21, 0x5E }, /* Q */
  { 0x7F, 0x09, 0x19, 0x29, 0x46 }, /* R */
  { 0x26, 0x49, 0x49, 0x49, 0x32 }, /* S */
  { 0x01, 0x01, 0x7F, 0x01, 0x01 }, /* T */
  { 0x3F, 0x40, 0x40, 0x40, 0x3F }, /* U */
  { 0x1F, 0x20, 0x40, 0x20, 0x1F }, /* V */
  { 0x3F, 0x40, 0x38, 0x40, 0x3F }, /* W */
  { 0x63, 0x14, 0x08, 0x14, 0x63 }, /* X */
  { 0x07, 0x08, 0x70, 0x08, 0x07 }, /* Y */
  { 0x61, 0x59, 0x49, 0x4D, 0x43 }, /* Z */
};

static void _draw_char(char c) {
  if (c < 0x20 || c > 0x5A) c = ' ';
  int idx = (int)(c - 0x20);
  if (idx < 0 || idx >= (int)(sizeof(_font5x7) / sizeof(_font5x7[0]))) idx = 0;
  for (int col = 0; col < 5; col++) {
    int byte_idx = _cy * PCD8544_WIDTH + _cx;
    if (_cx < PCD8544_WIDTH && byte_idx < (int)sizeof(_fb))
      _fb[byte_idx] = _font5x7[idx][col];
    _cx++;
  }
  _cx++;
}

void pcd8544_init(uint8_t sclk, uint8_t din, uint8_t dc, uint8_t cs, uint8_t rst) {
  _sclk = sclk; _din = din; _dc = dc; _cs = cs; _rst = rst;
  gpio_init(_sclk); gpio_init(_din); gpio_init(_dc); gpio_init(_cs); gpio_init(_rst);
  gpio_set_dir(_sclk, GPIO_OUT); gpio_set_dir(_din, GPIO_OUT); gpio_set_dir(_dc, GPIO_OUT);
  gpio_set_dir(_cs, GPIO_OUT); gpio_set_dir(_rst, GPIO_OUT);
  gpio_put(_cs, 1);
  gpio_put(_rst, 0);
  busy_wait_ms(10);
  gpio_put(_rst, 1);
  busy_wait_ms(10);
  _write_cmd(PCD8544_CMD_EXTENDED);
  _write_cmd(PCD8544_CMD_SET_BIAS);
  _write_cmd(PCD8544_CMD_SET_VOP | 0x35);
  _write_cmd(PCD8544_CMD_FUNC_SET);
  _write_cmd(PCD8544_CMD_DISP_CTL | PCD8544_CMD_NORMAL);
  memset(_fb, 0, sizeof(_fb));
  _cx = 0; _cy = 0;
}

void pcd8544_set_contrast(uint8_t value) {
  _write_cmd(PCD8544_CMD_EXTENDED);
  _write_cmd(PCD8544_CMD_SET_VOP | (value & 0x7F));
  _write_cmd(PCD8544_CMD_FUNC_SET);
}

void pcd8544_clear(void) {
  memset(_fb, 0, sizeof(_fb));
  _cx = 0; _cy = 0;
}

void pcd8544_display(void) {
  for (uint8_t row = 0; row < PCD8544_ROWS; row++) {
    _write_cmd(PCD8544_CMD_SET_X | 0);
    _write_cmd(PCD8544_CMD_SET_Y | row);
    for (uint8_t col = 0; col < PCD8544_WIDTH; col++)
      _write_data(_fb[row * PCD8544_WIDTH + col]);
  }
}

void pcd8544_set_cursor(uint8_t x, uint8_t y) {
  _cx = x; _cy = y;
}

void pcd8544_print(const char *str) {
  while (*str) {
    if (*str == '\n') {
      _cx = 0;
      _cy++;
      if (_cy >= PCD8544_ROWS) _cy = 0;
    } else {
      _draw_char(*str);
    }
    str++;
  }
}

void pcd8544_invert_row(uint8_t y) {
  if (y >= PCD8544_ROWS) {
    return;
  }
  for (uint8_t x = 0; x < PCD8544_WIDTH; x++) {
    size_t idx = (size_t)y * PCD8544_WIDTH + (size_t)x;
    if (idx < sizeof(_fb)) {
      _fb[idx] ^= 0xFFu;
    }
  }
}

void pcd8544_set_pixel(uint8_t x, uint8_t y, bool black) {
  if (x >= PCD8544_WIDTH || y >= PCD8544_HEIGHT) {
    return;
  }
  uint8_t bank = (uint8_t)(y / 8u);
  uint8_t bit = (uint8_t)(y % 8u);
  size_t idx = (size_t)bank * PCD8544_WIDTH + (size_t)x;
  if (idx >= sizeof(_fb)) {
    return;
  }
  if (black) {
    _fb[idx] |= (uint8_t)(1u << bit);
  } else {
    _fb[idx] = (uint8_t)(_fb[idx] & (uint8_t)~(1u << bit));
  }
}
