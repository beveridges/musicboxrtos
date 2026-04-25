/*
 * Minimal PCD8544 (Nokia 5110) driver — 84x48, soft SPI.
 * Text: ByteBounce 8px (repo tools/ByteBounce_8.c/.h), up to six lines on screen (48px).
 */
#ifndef PCD8544_H
#define PCD8544_H

#include <stdint.h>
#include <stdbool.h>

#define PCD8544_WIDTH   84
#define PCD8544_HEIGHT  48
#define PCD8544_ROWS    (PCD8544_HEIGHT / 8)
#define PCD8544_TEXT_LINES  6

void pcd8544_init(uint8_t sclk, uint8_t din, uint8_t dc, uint8_t cs, uint8_t rst);
void pcd8544_set_contrast(uint8_t value);
void pcd8544_clear(void);
void pcd8544_display(void);
/* y = text line 0..PCD8544_TEXT_LINES-1 (each line is 8px = one band). */
void pcd8544_set_cursor(uint8_t x, uint8_t y);
void pcd8544_print(const char *str);
/* Pixel width of str as drawn by pcd8544_print (ByteBounce advance per glyph + letter gap). */
uint16_t pcd8544_text_width(const char *str);
/* Invert one text line (8px) for inverse video (y = 0..PCD8544_TEXT_LINES-1). */
void pcd8544_invert_row(uint8_t y);
/* XOR-invert a pixel rectangle (e.g. narrower highlight than full row width). */
void pcd8544_invert_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
/* Set one pixel (0..83, 0..47). Black = foreground (set bits). */
void pcd8544_set_pixel(uint8_t x, uint8_t y, bool black);
void pcd8544_blit_bands(uint8_t start_band, uint8_t num_bands, const uint8_t *data);

#endif
