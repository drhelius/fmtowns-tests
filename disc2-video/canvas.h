#ifndef CANVAS_H
#define CANVAS_H

#include <stdint.h>
#include "video.h"

// Drawing in a layer's VRAM for the photo tests: a Data Book mode gives the CPU view, the line stride, the virtual
// and the effective size and the pixel format; colors are 16 logical ones, set in every palette

#define CANVAS_BLACK 0
#define CANVAS_WHITE 1
#define CANVAS_DARK 2
#define CANVAS_GRAY 3
#define CANVAS_RED 4
#define CANVAS_GREEN 5
#define CANVAS_BLUE 6
#define CANVAS_YELLOW 7
#define CANVAS_CYAN 8
#define CANVAS_MAGENTA 9
#define CANVAS_ORANGE 10
#define CANVAS_SKY 11
#define CANVAS_PINK 12
#define CANVAS_LIME 13
#define CANVAS_BROWN 14
#define CANVAS_LIGHT 15
#define CANVAS_CLEAR 0xFF

typedef struct
{
    uint32_t base;
    uint32_t pitch;
    uint16_t width;
    uint16_t height;
    uint16_t visible_width;
    uint16_t visible_height;
    uint8_t bpp;
    uint8_t mode;
} canvas_t;

void canvas_palettes(const video_mode_t* mode);
void canvas_set_color(int bank, uint8_t index, uint8_t red, uint8_t green, uint8_t blue);
void canvas_layer(canvas_t* canvas, int mode, int layer);
void canvas_window(canvas_t* canvas, const video_mode_t* mode, int layer);
uint32_t canvas_value(const canvas_t* canvas, uint8_t color);
void canvas_clear(const canvas_t* canvas, uint8_t color);
void canvas_fill(const canvas_t* canvas, int x, int y, int width, int height, uint8_t color);
void canvas_pixel(const canvas_t* canvas, int x, int y, uint8_t color);
void canvas_frame(const canvas_t* canvas, int x, int y, int width, int height, uint8_t color);
int canvas_text(const canvas_t* canvas, int x, int y, int scale, uint8_t color, uint8_t background, const char* text);
int canvas_printf(const canvas_t* canvas, int x, int y, int scale, uint8_t color, uint8_t background,
    const char* format, ...);

#endif /* CANVAS_H */
