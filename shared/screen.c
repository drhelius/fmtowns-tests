#include <stdarg.h>
#include "screen.h"
#include "font.h"
#include "fmt.h"
#include "towns.h"

#define CRTC_ADDRESS 0x0440
#define CRTC_DATA 0x0442
#define CRTC_CR0 0x1C
#define VIDEO_OUT_ADDRESS 0x0448
#define VIDEO_OUT_DATA 0x044A
#define VRAM_MASK_ADDRESS 0x0458
#define VRAM_MASK_DATA 0x045A
#define CRT_OUTPUT 0xFDA0
#define VRAM_SIZE 0x80000
#define CELL_PIXELS 0x01

static const uint16_t k_crtc[SCREEN_MODE_COUNT][32] =
{
    {
        0x0074, 0x0530, 0x0000, 0x0000, 0x0617, 0x0006, 0x000C, 0x0012, 0x020B, 0x00E7, 0x05E7, 0x00E7, 0x05E7,
        0x002A, 0x020A, 0x002A, 0x020A, 0x0000, 0x00E7, 0x0000, 0x0100, 0x0000, 0x00E7, 0x0000, 0x0100, 0x0056,
        0x0007, 0x0303, 0x8005, 0x0001, 0x0002, 0x0188
    },
    {
        0x0060, 0x02C0, 0x0000, 0x0000, 0x031F, 0x0000, 0x0004, 0x0000, 0x0419, 0x008A, 0x030A, 0x008A, 0x030A,
        0x0046, 0x0406, 0x0046, 0x0406, 0x0000, 0x008A, 0x0000, 0x0100, 0x0000, 0x008A, 0x0000, 0x0100, 0x0058,
        0x0000, 0x1111, 0x8005, 0x0002, 0x0000, 0x0192
    }
};

static const char* const k_mode_names[SCREEN_MODE_COUNT] = { "15K", "31K" };

// Green in bits 14-10, red in 9-5, blue in 4-0
static const uint16_t k_colors[] = { 0x0000, 0x7FFF, 0x4210, 0x7C00, 0x03E0, 0x7FE0, 0x7C1F };

// The text the shell builds, and what VRAM shows: only the cells that differ are drawn, since VRAM waits make a full
// redraw visible on a 16 MHz machine
static char s_text[SCREEN_ROWS][SCREEN_COLUMNS] SHELL_STATE;
static uint8_t s_color[SCREEN_ROWS][SCREEN_COLUMNS] SHELL_STATE;
static char s_shown_text[SCREEN_ROWS][SCREEN_COLUMNS] SHELL_STATE;
static uint8_t s_shown_color[SCREEN_ROWS][SCREEN_COLUMNS] SHELL_STATE;
static int s_dirty SHELL_STATE;
static int s_mode SHELL_STATE;
static uint32_t s_vram SHELL_STATE;
static uint16_t s_crc_table[256] SHELL_STATE;

static void draw_cell(int column, int row);

void scr_init(int mode, uint32_t vram_base)
{
    s_mode = mode;
    s_vram = vram_base;

    const uint16_t* crtc = k_crtc[mode];

    out8(CRTC_ADDRESS, CRTC_CR0);
    out16(CRTC_DATA, crtc[CRTC_CR0] & 0x7FFF);

    for (int i = 0; i < 32; i++)
    {
        if (i == CRTC_CR0)
            continue;

        out8(CRTC_ADDRESS, (uint8_t)i);
        out16(CRTC_DATA, crtc[i]);
    }

    out8(VIDEO_OUT_ADDRESS, 0x00);
    out8(VIDEO_OUT_DATA, 0x1F);
    out8(VIDEO_OUT_ADDRESS, 0x01);
    out8(VIDEO_OUT_DATA, 0x08);

    for (int i = 0; i < 2; i++)
    {
        out8(VRAM_MASK_ADDRESS, (uint8_t)i);
        out16(VRAM_MASK_DATA, 0xFFFF);
    }

    volatile uint32_t* vram = (volatile uint32_t*)s_vram;

    for (uint32_t i = 0; i < VRAM_SIZE / 4; i++)
        vram[i] = 0;

    for (int row = 0; row < SCREEN_ROWS; row++)
    {
        for (int column = 0; column < SCREEN_COLUMNS; column++)
        {
            s_text[row][column] = ' ';
            s_color[row][column] = COLOR_WHITE;
            s_shown_text[row][column] = ' ';
            s_shown_color[row][column] = COLOR_WHITE;
        }
    }

    s_dirty = 0;

    for (int i = 0; i < 256; i++)
    {
        uint16_t crc = (uint16_t)(i << 8);

        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 0x8000) != 0 ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);

        s_crc_table[i] = crc;
    }

    out8(CRTC_ADDRESS, CRTC_CR0);
    out16(CRTC_DATA, crtc[CRTC_CR0]);
    out8(CRT_OUTPUT, 0x0F);
}

int scr_mode(void)
{
    return s_mode;
}

const char* scr_mode_name(int mode)
{
    return k_mode_names[mode & 1];
}

void scr_clear(void)
{
    scr_clear_rows(0, SCREEN_ROWS - 1);
}

void scr_clear_rows(int first, int last)
{
    for (int row = first; row <= last; row++)
    {
        for (int column = 0; column < SCREEN_COLUMNS; column++)
        {
            s_text[row][column] = ' ';
            s_color[row][column] = COLOR_WHITE;
        }
    }

    s_dirty = 1;
}

void scr_text(int column, int row, uint8_t color, const char* text)
{
    if (row < 0 || row >= SCREEN_ROWS)
        return;

    for (; *text != 0 && column < SCREEN_COLUMNS; text++, column++)
    {
        if (column < 0)
            continue;

        s_text[row][column] = *text;
        s_color[row][column] = color;
    }

    s_dirty = 1;
}

void scr_printf(int column, int row, uint8_t color, const char* format, ...)
{
    char text[SCREEN_COLUMNS + 1];
    va_list arguments;
    va_start(arguments, format);
    fmt_vformat(text, sizeof(text), format, arguments);
    va_end(arguments);
    scr_text(column, row, color, text);
}

// Pixels go straight to VRAM over the text drawn so far; their cells are marked so the next text there redraws them
void scr_fill(int x, int y, int width, int height, uint8_t color)
{
    uint16_t value = k_colors[color];

    scr_present();

    for (int row = y / 8; row <= (y + height - 1) / 8 && row < SCREEN_ROWS; row++)
    {
        for (int column = x / 8; column <= (x + width - 1) / 8 && column < SCREEN_COLUMNS; column++)
        {
            s_text[row][column] = CELL_PIXELS;
            s_shown_text[row][column] = CELL_PIXELS;
        }
    }

    for (int line = y; line < y + height && line < SCREEN_HEIGHT; line++)
    {
        volatile uint16_t* pixel = (volatile uint16_t*)(s_vram + (uint32_t)line * SCREEN_LINE_BYTES) + x;

        for (int i = 0; i < width && x + i < SCREEN_WIDTH; i++)
            pixel[i] = value;
    }
}

void scr_present(void)
{
    if (!s_dirty)
        return;

    for (int row = 0; row < SCREEN_ROWS; row++)
    {
        for (int column = 0; column < SCREEN_COLUMNS; column++)
        {
            if (s_text[row][column] == s_shown_text[row][column] && s_color[row][column] == s_shown_color[row][column])
                continue;

            draw_cell(column, row);
            s_shown_text[row][column] = s_text[row][column];
            s_shown_color[row][column] = s_color[row][column];
        }
    }

    s_dirty = 0;
}

char scr_char_at(int column, int row)
{
    return s_text[row][column];
}

// CRC-16/CCITT-FALSE of the text of the rows, so a transcription of a photo can be checked
uint16_t scr_checksum(int first_row, int last_row)
{
    uint16_t crc = 0xFFFF;

    for (int row = first_row; row <= last_row; row++)
        crc = scr_crc16((const uint8_t*)s_text[row], SCREEN_COLUMNS, crc);

    return crc;
}

uint16_t scr_crc16(const uint8_t* data, uint32_t size, uint16_t crc)
{
    for (uint32_t i = 0; i < size; i++)
        crc = (uint16_t)((crc << 8) ^ s_crc_table[(uint8_t)((crc >> 8) ^ data[i])]);

    return crc;
}

static void draw_cell(int column, int row)
{
    const uint8_t* glyph = font_glyph((uint8_t)s_text[row][column]);
    uint32_t color = k_colors[s_color[row][column]];
    volatile uint32_t* line = (volatile uint32_t*)(s_vram + (uint32_t)row * 8 * SCREEN_LINE_BYTES + column * 16);

    for (int y = 0; y < 8; y++)
    {
        uint8_t bits = glyph[y];

        for (int x = 0; x < 4; x++)
        {
            line[x] = ((bits & 0x80) != 0 ? color : 0) | ((bits & 0x40) != 0 ? color << 16 : 0);
            bits = (uint8_t)(bits << 2);
        }

        line += SCREEN_LINE_BYTES / 4;
    }
}
