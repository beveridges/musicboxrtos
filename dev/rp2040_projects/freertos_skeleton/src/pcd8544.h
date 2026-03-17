/*
 * Minimal PCD8544 (Nokia 5110) driver — 84x48, soft SPI.
 */
#ifndef PCD8544_H
#define PCD8544_H

#include <stdint.h>
#include <stdbool.h>

#define PCD8544_WIDTH   84
#define PCD8544_HEIGHT  48
#define PCD8544_ROWS    (PCD8544_HEIGHT / 8)

void pcd8544_init(uint8_t sclk, uint8_t din, uint8_t dc, uint8_t cs, uint8_t rst);
void pcd8544_set_contrast(uint8_t value);
void pcd8544_clear(void);
void pcd8544_display(void);
void pcd8544_set_cursor(uint8_t x, uint8_t y);
void pcd8544_print(const char *str);

#endif
