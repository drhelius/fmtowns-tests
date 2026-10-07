#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

// 80x25 text drawn with the ANK font into the FM-R compatible planes the BIOS leaves on screen
// Both sit in low memory at the same addresses on every model, DX and SX alike

#define VIDEO_COLUMNS 80
#define VIDEO_ROWS 25

#define COLOR_BLACK 0
#define COLOR_BLUE 1
#define COLOR_RED 2
#define COLOR_MAGENTA 3
#define COLOR_GREEN 4
#define COLOR_CYAN 5
#define COLOR_YELLOW 6
#define COLOR_WHITE 7
#define COLOR_GRAY 8
#define COLOR_LIGHT_BLUE 9
#define COLOR_LIGHT_RED 10
#define COLOR_LIGHT_MAGENTA 11
#define COLOR_LIGHT_GREEN 12
#define COLOR_LIGHT_CYAN 13
#define COLOR_LIGHT_YELLOW 14
#define COLOR_BRIGHT_WHITE 15

void video_init(void);
void video_clear(void);
void video_color(uint8_t color);
void video_locate(int column, int row);
void video_print(const char* text);
void video_print_hex(uint32_t value, int digits);
void video_print_dec(uint32_t value);

#endif /* VIDEO_H */
