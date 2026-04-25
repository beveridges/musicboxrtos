/*
 * Minimal PCD8544 (Nokia 5110) driver — soft SPI, 84x48 framebuffer, ByteBounce 8px font.
 */
#include "pcd8544.h"
#include "ByteBounce_8.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <stddef.h>
#include <string.h>

#define BYTEBOUNCE_GLYPH_COUNT ((BYTEBOUNCE_LAST_CHAR - BYTEBOUNCE_FIRST_CHAR) + 1)
#define BYTEBOUNCE_BANDS ((uint8_t)(BYTEBOUNCE_HEIGHT / 8u))

static uint8_t _sclk, _din, _dc, _cs, _rst;
static uint8_t _fb[PCD8544_WIDTH * PCD8544_ROWS];
static uint8_t _cx;
static uint8_t _line; /* 0 .. PCD8544_TEXT_LINES-1 */

#define PCD8544_CMD_FUNC_SET    0x20
#define PCD8544_CMD_EXTENDED    0x21
#define PCD8544_CMD_SET_BIAS    0x14
#define PCD8544_CMD_SET_VOP     0x80
#define PCD8544_CMD_DISP_CTL    0x08
#define PCD8544_CMD_NORMAL      0x0C
#define PCD8544_CMD_SET_X       0x80
#define PCD8544_CMD_SET_Y       0x40

static inline uint8_t _top_band(void) {
  return (uint8_t)(_line * BYTEBOUNCE_BANDS);
}

static inline uint16_t _glyph_end_index(int idx) {
  if (idx < BYTEBOUNCE_GLYPH_COUNT - 1) {
    return ByteBounce8_Offsets[idx + 1];
  }
  const uint8_t *p = &ByteBounce8_Bitmaps[ByteBounce8_Offsets[idx]];
  uint8_t w = p[0];
  return (uint16_t)(ByteBounce8_Offsets[idx] + 1u + (uint16_t)w * (uint16_t)BYTEBOUNCE_BANDS);
}

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

static uint8_t _char_advance_pixels(char c) {
  unsigned char uc = (unsigned char)c;
  if (uc < BYTEBOUNCE_FIRST_CHAR || uc > BYTEBOUNCE_LAST_CHAR) {
    uc = ' ';
  }
  int idx = (int)uc - BYTEBOUNCE_FIRST_CHAR;
  uint16_t start = ByteBounce8_Offsets[idx];
  const uint8_t *p = &ByteBounce8_Bitmaps[start];
  uint8_t gw = p[0];
  return (uint8_t)(gw + 1u);
}

uint16_t pcd8544_text_width(const char *str) {
  uint16_t sum = 0;
  if (str == NULL) {
    return 0;
  }
  while (*str != '\0' && *str != '\n') {
    sum += (uint16_t)_char_advance_pixels(*str);
    str++;
  }
  return sum;
}

static void _draw_char(char c) {
  unsigned char uc = (unsigned char)c;
  if (uc < BYTEBOUNCE_FIRST_CHAR || uc > BYTEBOUNCE_LAST_CHAR) {
    uc = ' ';
  }
  int idx = (int)uc - BYTEBOUNCE_FIRST_CHAR;
  uint16_t start = ByteBounce8_Offsets[idx];
  uint16_t end = _glyph_end_index(idx);
  const uint8_t *p = &ByteBounce8_Bitmaps[start];
  size_t avail = (size_t)(end - start);
  if (avail < 1u) {
    return;
  }
  uint8_t w = p[0];
  if ((size_t)(1u + (unsigned)w * (unsigned)BYTEBOUNCE_BANDS) > avail) {
    return;
  }
  uint8_t b0 = _top_band();
  if ((unsigned)b0 + (unsigned)BYTEBOUNCE_BANDS > (unsigned)PCD8544_ROWS) {
    return;
  }
  for (unsigned col = 0; col < (unsigned)w; col++) {
    for (uint8_t bi = 0; bi < BYTEBOUNCE_BANDS; bi++) {
      uint8_t bits = p[1u + (size_t)col * (size_t)BYTEBOUNCE_BANDS + (size_t)bi];
      uint8_t band = (uint8_t)(b0 + bi);
      int ii = (int)band * PCD8544_WIDTH + _cx;
      if (_cx < PCD8544_WIDTH && ii >= 0 && ii < (int)sizeof(_fb)) {
        _fb[ii] = bits;
      }
    }
    _cx++;
  }
  if (_cx < PCD8544_WIDTH) {
    _cx++;
  }
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
  _cx = 0; _line = 0;
}

void pcd8544_set_contrast(uint8_t value) {
  _write_cmd(PCD8544_CMD_EXTENDED);
  _write_cmd(PCD8544_CMD_SET_VOP | (value & 0x7F));
  _write_cmd(PCD8544_CMD_FUNC_SET);
}

void pcd8544_clear(void) {
  memset(_fb, 0, sizeof(_fb));
  _cx = 0; _line = 0;
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
  _cx = x;
  _line = y;
  if (_line >= PCD8544_TEXT_LINES) {
    _line = (uint8_t)(PCD8544_TEXT_LINES - 1u);
  }
}

void pcd8544_print(const char *str) {
  while (*str) {
    if (*str == '\n') {
      _cx = 0;
      _line++;
      if (_line >= PCD8544_TEXT_LINES) {
        _line = 0;
      }
    } else {
      _draw_char(*str);
    }
    str++;
  }
}

void pcd8544_invert_row(uint8_t y) {
  if (y >= PCD8544_TEXT_LINES) {
    return;
  }
  uint8_t b0 = (uint8_t)(y * BYTEBOUNCE_BANDS);
  for (uint8_t bx = 0; bx < BYTEBOUNCE_BANDS; bx++) {
    uint8_t band = (uint8_t)(b0 + bx);
    if (band >= PCD8544_ROWS) {
      break;
    }
    for (uint8_t x = 0; x < PCD8544_WIDTH; x++) {
      size_t idx = (size_t)band * PCD8544_WIDTH + (size_t)x;
      if (idx < sizeof(_fb)) {
        _fb[idx] ^= 0xFFu;
      }
    }
  }
}

void pcd8544_invert_rect(uint8_t x0, uint8_t y0, uint8_t w, uint8_t h) {
  if (w == 0 || h == 0) {
    return;
  }
  for (uint16_t dy = 0; dy < (uint16_t)h; dy++) {
    uint16_t y = (uint16_t)y0 + dy;
    if (y >= (uint16_t)PCD8544_HEIGHT) {
      break;
    }
    for (uint16_t dx = 0; dx < (uint16_t)w; dx++) {
      uint16_t x = (uint16_t)x0 + dx;
      if (x >= (uint16_t)PCD8544_WIDTH) {
        continue;
      }
      uint8_t bank = (uint8_t)(y / 8u);
      uint8_t bit = (uint8_t)(y % 8u);
      size_t idx = (size_t)bank * PCD8544_WIDTH + (size_t)x;
      if (idx < sizeof(_fb)) {
        _fb[idx] ^= (uint8_t)(1u << bit);
      }
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

void pcd8544_blit_bands(uint8_t start_band, uint8_t num_bands, const uint8_t *data) {
  if (data == NULL || num_bands == 0) {
    return;
  }
  for (uint8_t b = 0; b < num_bands; b++) {
    uint8_t band = (uint8_t)(start_band + b);
    if (band >= PCD8544_ROWS) {
      break;
    }
    memcpy(&_fb[(size_t)band * PCD8544_WIDTH], data + (size_t)b * PCD8544_WIDTH, PCD8544_WIDTH);
  }
}
