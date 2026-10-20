#include <stdarg.h>
#include "canvas.h"
#include "fmt.h"
#include "font.h"
#include "machine.h"
#include "towns.h"

#define SINGLE_VIEW 0x100000u
#define LAYER1_PAGE 0x40000u

typedef struct
{
    uint16_t width;
    uint16_t height;
    uint16_t visible_width;
    uint16_t visible_height;
    uint16_t pitch;
    uint8_t bpp;
    uint8_t single;
} geometry_t;

// Table I-4-1 and the address views of 4.3.3, by mode number
static const geometry_t k_geometry[19] =
{
    { 0, 0, 0, 0, 0, 0, 0 },
    { 640, 400, 640, 400, 0x140, 4, 0 },
    { 640, 400, 640, 200, 0x140, 4, 0 },
    { 1024, 512, 640, 480, 0x200, 4, 0 },
    { 1024, 512, 640, 400, 0x200, 4, 0 },
    { 256, 512, 256, 256, 0x200, 16, 0 },
    { 256, 512, 256, 256, 0x200, 16, 0 },
    { 256, 512, 256, 240, 0x200, 16, 0 },
    { 256, 512, 256, 240, 0x200, 16, 0 },
    { 512, 256, 360, 240, 0x400, 16, 0 },
    { 512, 256, 320, 240, 0x400, 16, 0 },
    { 512, 256, 320, 240, 0x400, 16, 0 },
    { 1024, 512, 640, 480, 0x400, 8, 1 },
    { 1024, 512, 640, 400, 0x400, 8, 1 },
    { 1024, 512, 720, 480, 0x400, 8, 1 },
    { 512, 512, 320, 480, 0x400, 16, 1 },
    { 512, 512, 320, 480, 0x400, 16, 1 },
    { 512, 512, 512, 480, 0x400, 16, 1 },
    { 512, 512, 512, 480, 0x400, 16, 1 }
};

static const uint8_t k_colors[16][3] =
{
    { 0, 0, 0 }, { 255, 255, 255 }, { 64, 64, 64 }, { 128, 128, 128 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 },
    { 255, 255, 0 }, { 0, 255, 255 }, { 255, 0, 255 }, { 255, 128, 0 }, { 64, 128, 255 }, { 255, 96, 160 },
    { 128, 255, 0 }, { 128, 64, 0 }, { 192, 192, 192 }
};

static const uint8_t k_banks[3] = { 0x00, 0x20, 0x10 };

static void put(const canvas_t* canvas, int x, int y, uint32_t value);

void canvas_set_color(int bank, uint8_t index, uint8_t red, uint8_t green, uint8_t blue)
{
    out8(OUTPUT_ADDRESS, 0x01);
    out8(OUTPUT_DATA, (uint8_t)(0x08 | k_banks[bank]));
    out8(0xFD90, index);
    out8(0xFD92, blue);
    out8(0xFD94, red);
    out8(0xFD96, green);
}

// The 16 logical colors at 0-15 of both 16-color palettes and the 256-color one, then the mode's own selection
void canvas_palettes(const video_mode_t* mode)
{
    for (int bank = 0; bank < 3; bank++)
    {
        for (int i = 0; i < 16; i++)
            canvas_set_color(bank, (uint8_t)i, k_colors[i][0], k_colors[i][1], k_colors[i][2]);
    }

    out8(OUTPUT_ADDRESS, 0x01);
    out8(OUTPUT_DATA, mode->output[1]);
}

void canvas_layer(canvas_t* canvas, int mode, int layer)
{
    const geometry_t* geometry = &k_geometry[mode >= 1 && mode <= 18 ? mode : 12];

    canvas->mode = (uint8_t)mode;
    canvas->width = geometry->width;
    canvas->height = geometry->height;
    canvas->visible_width = geometry->visible_width;
    canvas->visible_height = geometry->visible_height;
    canvas->pitch = geometry->pitch;
    canvas->bpp = geometry->bpp;
    canvas->base = g_machine.vram + (geometry->single ? SINGLE_VIEW : layer != 0 ? LAYER1_PAGE : 0);
}

// Window dots over the horizontal zoom, window half-lines over two and the vertical zoom, doubled when FO interlaces
void canvas_window(canvas_t* canvas, const video_mode_t* mode, int layer)
{
    uint16_t zoom = (uint16_t)(video_register(mode, CRTC_ZOOM) >> (layer * 8));
    uint32_t dots = (uint16_t)(video_register(mode, CRTC_HDE0 + layer * 2) -
        video_register(mode, CRTC_HDS0 + layer * 2));
    uint32_t half_lines = (uint16_t)(video_register(mode, CRTC_VDE0 + layer * 2) -
        video_register(mode, CRTC_VDS0 + layer * 2));
    int interlaced = video_register(mode, CRTC_FO0 + layer * 4) != 0;

    canvas->visible_width = (uint16_t)(dots / ((zoom & 0x0F) + 1u));
    canvas->visible_height = (uint16_t)(half_lines / 2 / (((zoom >> 4) & 0x0F) + 1u) * (interlaced ? 2 : 1));
}

uint32_t canvas_value(const canvas_t* canvas, uint8_t color)
{
    if (color == CANVAS_CLEAR)
        return canvas->bpp == 16 ? 0x8000u : 0;

    if (canvas->bpp != 16)
        return color & 0x0F;

    const uint8_t* rgb = k_colors[color & 0x0F];
    return (uint32_t)((rgb[1] >> 3) << 10 | (rgb[0] >> 3) << 5 | (rgb[2] >> 3));
}

static void put(const canvas_t* canvas, int x, int y, uint32_t value)
{
    uint32_t row = canvas->base + (uint32_t)y * canvas->pitch;

    if (canvas->bpp == 16)
        MMIO16(row + (uint32_t)x * 2) = (uint16_t)value;
    else if (canvas->bpp == 8)
        MMIO8(row + (uint32_t)x) = (uint8_t)value;
    else
    {
        uint32_t address = row + (uint32_t)x / 2;
        uint8_t byte = MMIO8(address);
        MMIO8(address) = (x & 1) != 0 ? (uint8_t)((byte & 0x0F) | (value << 4)) : (uint8_t)((byte & 0xF0) | value);
    }
}

void canvas_pixel(const canvas_t* canvas, int x, int y, uint8_t color)
{
    if (x >= 0 && y >= 0 && x < canvas->width && y < canvas->height)
        put(canvas, x, y, canvas_value(canvas, color));
}

// Whole dwords where the run allows, pixels at the ends
void canvas_fill(const canvas_t* canvas, int x, int y, int width, int height, uint8_t color)
{
    uint32_t value = canvas_value(canvas, color);
    int per_dword = 32 / canvas->bpp;
    uint32_t pattern = canvas->bpp == 16 ? value | value << 16 : canvas->bpp == 8 ? value * 0x01010101u :
        value * 0x11111111u;
    int right = x + width < canvas->width ? x + width : canvas->width;
    int bottom = y + height < canvas->height ? y + height : canvas->height;

    x = x > 0 ? x : 0;
    y = y > 0 ? y : 0;

    for (int row = y; row < bottom; row++)
    {
        int column = x;

        while (column < right && (column % per_dword) != 0)
            put(canvas, column++, row, value);

        volatile uint32_t* line = (volatile uint32_t*)(canvas->base + (uint32_t)row * canvas->pitch +
            (uint32_t)column * canvas->bpp / 8);

        for (; right - column >= per_dword; column += per_dword)
            *line++ = pattern;

        while (column < right)
            put(canvas, column++, row, value);
    }
}

void canvas_clear(const canvas_t* canvas, uint8_t color)
{
    canvas_fill(canvas, 0, 0, canvas->width, canvas->height, color);
}

void canvas_frame(const canvas_t* canvas, int x, int y, int width, int height, uint8_t color)
{
    canvas_fill(canvas, x, y, width, 1, color);
    canvas_fill(canvas, x, y + height - 1, width, 1, color);
    canvas_fill(canvas, x, y, 1, height, color);
    canvas_fill(canvas, x + width - 1, y, 1, height, color);
}

// 8x8 glyphs at a scale over their background; returns the width drawn
int canvas_text(const canvas_t* canvas, int x, int y, int scale, uint8_t color, uint8_t background, const char* text)
{
    uint32_t front = canvas_value(canvas, color);
    uint32_t back = canvas_value(canvas, background);
    int start = x;

    for (; *text != 0; text++, x += 8 * scale)
    {
        const uint8_t* glyph = font_glyph((uint8_t)*text);

        for (int row = 0; row < 8 * scale; row++)
        {
            uint8_t bits = glyph[row / scale];
            int py = y + row;

            if (py < 0 || py >= canvas->height)
                continue;

            for (int column = 0; column < 8 * scale; column++)
            {
                int px = x + column;

                if (px >= 0 && px < canvas->width)
                    put(canvas, px, py, (bits & (0x80 >> (column / scale))) != 0 ? front : back);
            }
        }
    }

    return x - start;
}

int canvas_printf(const canvas_t* canvas, int x, int y, int scale, uint8_t color, uint8_t background,
    const char* format, ...)
{
    char text[64];
    va_list arguments;
    va_start(arguments, format);
    fmt_vformat(text, sizeof(text), format, arguments);
    va_end(arguments);
    return canvas_text(canvas, x, y, scale, color, background, text);
}
