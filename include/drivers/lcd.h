/* ST7789V3 240x240 LCD on FSMC (8-bit bus). Not thread-safe: only the UI task draws. */
#ifndef DRIVERS_LCD_H
#define DRIVERS_LCD_H

#include <stdbool.h>
#include <stdint.h>

#define LCD_W 240
#define LCD_H 240

/* RGB565 */
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define COLOR_BLACK  0x0000
#define COLOR_WHITE  0xFFFF
#define COLOR_RED    0xF800
#define COLOR_GREEN  0x07E0
#define COLOR_BLUE   0x001F
#define COLOR_YELLOW 0xFFE0
#define COLOR_CYAN   0x07FF
#define COLOR_GRAY   0x8410
#define COLOR_ORANGE RGB565(255, 140, 0)

typedef struct {
    bool ok;
    uint8_t id[3]; /* RDDID (0x04) bytes after the dummy read */
} lcd_info_t;

lcd_info_t lcd_init(void);
void lcd_backlight(bool on);
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void lcd_fill(uint16_t color);
/* Draws text with the 8x16 font scaled by `scale`; returns the x after the last glyph. */
uint16_t lcd_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);

#endif /* DRIVERS_LCD_H */
