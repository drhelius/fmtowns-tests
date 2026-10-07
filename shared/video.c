#include "video.h"
#include "towns.h"

#define FMR_PLANES ((volatile uint8_t*)0x000C0000)
#define FMR_ANK16_FONT ((volatile uint8_t*)0x000CB000)
#define FMR_WRITE_PLANES MMIO8(0x000CFF81)
#define FMR_DISPLAY_MODE MMIO8(0x000CFF82)
#define FMR_CPU_PAGE MMIO8(0x000CFF83)
#define FMR_ANK_SELECT MMIO8(0x000CFF99)

#define FMR_BYTES_PER_LINE 80
#define FMR_LINES 400
#define FONT_HEIGHT 16

static uint8_t s_font[256 * FONT_HEIGHT];
static uint8_t s_color = COLOR_BRIGHT_WHITE;
static int s_column = 0;
static int s_row = 0;

static void draw_char(int column, int row, uint8_t code);
static void new_line(void);

void video_init(void)
{
    // All planes shown, CPU on page 0
    FMR_DISPLAY_MODE = 0x27;
    FMR_CPU_PAGE = 0x00;

    FMR_ANK_SELECT = 0x01;

    for (int i = 0; i < 256 * FONT_HEIGHT; i++)
        s_font[i] = FMR_ANK16_FONT[i];

    FMR_ANK_SELECT = 0x00;

    video_clear();
}

void video_clear(void)
{
    FMR_WRITE_PLANES = 0x0F;

    for (int i = 0; i < FMR_BYTES_PER_LINE * FMR_LINES; i++)
        FMR_PLANES[i] = 0;

    s_column = 0;
    s_row = 0;
}

void video_color(uint8_t color)
{
    s_color = color & 0x0F;
}

void video_locate(int column, int row)
{
    s_column = column;
    s_row = row;
}

void video_print(const char* text)
{
    while (*text != 0)
    {
        char c = *text++;

        if (c == '\n')
        {
            new_line();
            continue;
        }

        if (s_column >= VIDEO_COLUMNS)
            new_line();

        draw_char(s_column, s_row, (uint8_t)c);
        s_column++;
    }
}

void video_print_hex(uint32_t value, int digits)
{
    static const char k_hex[] = "0123456789ABCDEF";
    char text[9];

    if (digits < 1)
        digits = 1;
    else if (digits > 8)
        digits = 8;

    for (int i = 0; i < digits; i++)
        text[i] = k_hex[(value >> ((digits - 1 - i) * 4)) & 0x0F];

    text[digits] = 0;
    video_print(text);
}

void video_print_dec(uint32_t value)
{
    char text[11];
    int i = 10;

    text[i] = 0;

    do
    {
        text[--i] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);

    video_print(&text[i]);
}

// The glyph goes to the planes of the color and zero to the others, so the cell needs no clearing
static void draw_char(int column, int row, uint8_t code)
{
    if (row >= VIDEO_ROWS)
        return;

    volatile uint8_t* cell = FMR_PLANES + row * FONT_HEIGHT * FMR_BYTES_PER_LINE + column;
    const uint8_t* glyph = &s_font[code * FONT_HEIGHT];

    for (int y = 0; y < FONT_HEIGHT; y++)
    {
        FMR_WRITE_PLANES = s_color;
        cell[y * FMR_BYTES_PER_LINE] = glyph[y];
        FMR_WRITE_PLANES = (uint8_t)(~s_color & 0x0F);
        cell[y * FMR_BYTES_PER_LINE] = 0;
    }
}

static void new_line(void)
{
    s_column = 0;
    s_row++;
}
