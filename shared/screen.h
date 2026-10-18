#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

// One 320x240 32,768-color layer holding 40x30 cells of the 8x8 font
// 15 kHz is the Data Book's preset 14 (mode 11, 262 lines), 31 kHz is preset 28 with both layers zoomed twice

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define SCREEN_COLUMNS 40
#define SCREEN_ROWS 30
#define SCREEN_LINE_BYTES 1024

#define SCREEN_LEFT 1
#define SCREEN_RIGHT 38
#define SCREEN_TOP 2
#define SCREEN_BOTTOM 28
#define SCREEN_SAFE_COLUMNS 38

#define SCREEN_MODE_15K 0
#define SCREEN_MODE_31K 1
#define SCREEN_MODE_COUNT 2

#define COLOR_BLACK 0
#define COLOR_WHITE 1
#define COLOR_GRAY 2
#define COLOR_GREEN 3
#define COLOR_RED 4
#define COLOR_YELLOW 5
#define COLOR_CYAN 6

void scr_init(int mode, uint32_t vram_base);
int scr_mode(void);
const char* scr_mode_name(int mode);
void scr_clear(void);
void scr_clear_rows(int first, int last);
void scr_text(int column, int row, uint8_t color, const char* text);
void scr_printf(int column, int row, uint8_t color, const char* format, ...);
void scr_fill(int x, int y, int width, int height, uint8_t color);
void scr_present(void);
char scr_char_at(int column, int row);
uint16_t scr_checksum(int first_row, int last_row);
uint16_t scr_crc16(const uint8_t* data, uint32_t size, uint16_t crc);

#endif /* SCREEN_H */
