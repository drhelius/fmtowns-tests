#include "font.h"
#include "towns.h"

// Bold 7x8 glyphs in an 8x8 cell, uppercase only: slashed zero, flagged 1, flat-topped 5, flat-sided B and D
// keep 0/O/D, 1/I, 5/S and 8/B apart in a photo of a TV

#define FONT_FIRST 0x20
#define FONT_LAST 0x5F

static const char* const k_glyphs[][8] =
{
    { ".......", ".......", ".......", ".......", ".......", ".......", ".......", "......." }, // space
    { "..XX...", "..XX...", "..XX...", "..XX...", "..XX...", ".......", "..XX...", "......." }, // !
    { ".XX.XX.", ".XX.XX.", ".XX.XX.", ".......", ".......", ".......", ".......", "......." }, // "
    { ".XX.XX.", "XXXXXXX", ".XX.XX.", ".XX.XX.", ".XX.XX.", "XXXXXXX", ".XX.XX.", "......." }, // #
    { "..XX...", ".XXXXX.", "XX.....", ".XXXX..", "....XX.", "XXXXX..", "..XX...", "......." }, // $
    { "XX...X.", "XX..XX.", "...XX..", "..XX...", ".XX....", "XX..XX.", "X...XX.", "......." }, // %
    { ".XXX...", "XX.XX..", ".XXX...", "XXX.XX.", "XX.XXX.", "XX..XX.", ".XXX.XX", "......." }, // &
    { "..XX...", "..XX...", ".XX....", ".......", ".......", ".......", ".......", "......." }, // '
    { "...XX..", "..XX...", ".XX....", ".XX....", ".XX....", "..XX...", "...XX..", "......." }, // (
    { ".XX....", "..XX...", "...XX..", "...XX..", "...XX..", "..XX...", ".XX....", "......." }, // )
    { ".......", ".XX.XX.", "..XXX..", "XXXXXXX", "..XXX..", ".XX.XX.", ".......", "......." }, // *
    { ".......", "..XX...", "..XX...", "XXXXXX.", "..XX...", "..XX...", ".......", "......." }, // +
    { ".......", ".......", ".......", ".......", ".......", "..XX...", "..XX...", ".XX...." }, // ,
    { ".......", ".......", ".......", "XXXXXX.", ".......", ".......", ".......", "......." }, // -
    { ".......", ".......", ".......", ".......", ".......", "..XX...", "..XX...", "......." }, // .
    { ".....XX", "....XX.", "...XX..", "..XX...", ".XX....", "XX.....", "X......", "......." }, // /
    { ".XXXXX.", "XX...XX", "XX..XXX", "XX.X.XX", "XXX..XX", "XX...XX", ".XXXXX.", "......." }, // 0
    { "..XX...", ".XXX...", "XXXX...", "..XX...", "..XX...", "..XX...", "XXXXXX.", "......." }, // 1
    { ".XXXXX.", "XX...XX", ".....XX", "...XXX.", ".XXX...", "XX.....", "XXXXXXX", "......." }, // 2
    { ".XXXXX.", "XX...XX", ".....XX", "..XXXX.", ".....XX", "XX...XX", ".XXXXX.", "......." }, // 3
    { "...XXX.", "..XXXX.", ".XX.XX.", "XX..XX.", "XXXXXXX", "....XX.", "....XX.", "......." }, // 4
    { "XXXXXXX", "XX.....", "XXXXXX.", ".....XX", ".....XX", "XX...XX", ".XXXXX.", "......." }, // 5
    { "..XXXX.", ".XX....", "XX.....", "XXXXXX.", "XX...XX", "XX...XX", ".XXXXX.", "......." }, // 6
    { "XXXXXXX", ".....XX", "....XX.", "...XX..", "..XX...", "..XX...", "..XX...", "......." }, // 7
    { ".XXXXX.", "XX...XX", "XX...XX", ".XXXXX.", "XX...XX", "XX...XX", ".XXXXX.", "......." }, // 8
    { ".XXXXX.", "XX...XX", "XX...XX", ".XXXXXX", ".....XX", "....XX.", ".XXXX..", "......." }, // 9
    { ".......", "..XX...", "..XX...", ".......", "..XX...", "..XX...", ".......", "......." }, // :
    { ".......", "..XX...", "..XX...", ".......", "..XX...", "..XX...", ".XX....", "......." }, // ;
    { "....XX.", "...XX..", "..XX...", ".XX....", "..XX...", "...XX..", "....XX.", "......." }, // <
    { ".......", ".......", "XXXXXX.", ".......", "XXXXXX.", ".......", ".......", "......." }, // =
    { ".XX....", "..XX...", "...XX..", "....XX.", "...XX..", "..XX...", ".XX....", "......." }, // >
    { ".XXXXX.", "XX...XX", "....XX.", "...XX..", "...XX..", ".......", "...XX..", "......." }, // ?
    { ".XXXXX.", "XX...XX", "XX.XXXX", "XX.XXXX", "XX.XXX.", "XX.....", ".XXXXX.", "......." }, // @
    { "..XXX..", ".XX.XX.", "XX...XX", "XX...XX", "XXXXXXX", "XX...XX", "XX...XX", "......." }, // A
    { "XXXXXX.", "XX...XX", "XX...XX", "XXXXXX.", "XX...XX", "XX...XX", "XXXXXX.", "......." }, // B
    { ".XXXXX.", "XX...XX", "XX.....", "XX.....", "XX.....", "XX...XX", ".XXXXX.", "......." }, // C
    { "XXXXX..", "XX..XX.", "XX...XX", "XX...XX", "XX...XX", "XX..XX.", "XXXXX..", "......." }, // D
    { "XXXXXXX", "XX.....", "XX.....", "XXXXXX.", "XX.....", "XX.....", "XXXXXXX", "......." }, // E
    { "XXXXXXX", "XX.....", "XX.....", "XXXXXX.", "XX.....", "XX.....", "XX.....", "......." }, // F
    { ".XXXXX.", "XX...XX", "XX.....", "XX..XXX", "XX...XX", "XX...XX", ".XXXXXX", "......." }, // G
    { "XX...XX", "XX...XX", "XX...XX", "XXXXXXX", "XX...XX", "XX...XX", "XX...XX", "......." }, // H
    { "XXXXXX.", "..XX...", "..XX...", "..XX...", "..XX...", "..XX...", "XXXXXX.", "......." }, // I
    { "...XXXX", "....XX.", "....XX.", "....XX.", "XX..XX.", "XX..XX.", ".XXXX..", "......." }, // J
    { "XX...XX", "XX..XX.", "XX.XX..", "XXXX...", "XX.XX..", "XX..XX.", "XX...XX", "......." }, // K
    { "XX.....", "XX.....", "XX.....", "XX.....", "XX.....", "XX.....", "XXXXXXX", "......." }, // L
    { "XX...XX", "XXX.XXX", "XXXXXXX", "XX.X.XX", "XX...XX", "XX...XX", "XX...XX", "......." }, // M
    { "XX...XX", "XXX..XX", "XXXX.XX", "XX.XXXX", "XX..XXX", "XX...XX", "XX...XX", "......." }, // N
    { ".XXXXX.", "XX...XX", "XX...XX", "XX...XX", "XX...XX", "XX...XX", ".XXXXX.", "......." }, // O
    { "XXXXXX.", "XX...XX", "XX...XX", "XXXXXX.", "XX.....", "XX.....", "XX.....", "......." }, // P
    { ".XXXXX.", "XX...XX", "XX...XX", "XX...XX", "XX.X.XX", "XX..XX.", ".XXX.XX", "......." }, // Q
    { "XXXXXX.", "XX...XX", "XX...XX", "XXXXXX.", "XX.XX..", "XX..XX.", "XX...XX", "......." }, // R
    { ".XXXXX.", "XX...XX", "XX.....", ".XXXXX.", ".....XX", "XX...XX", ".XXXXX.", "......." }, // S
    { "XXXXXX.", "..XX...", "..XX...", "..XX...", "..XX...", "..XX...", "..XX...", "......." }, // T
    { "XX...XX", "XX...XX", "XX...XX", "XX...XX", "XX...XX", "XX...XX", ".XXXXX.", "......." }, // U
    { "XX...XX", "XX...XX", "XX...XX", "XX...XX", ".XX.XX.", "..XXX..", "...X...", "......." }, // V
    { "XX...XX", "XX...XX", "XX...XX", "XX.X.XX", "XXXXXXX", "XXX.XXX", "XX...XX", "......." }, // W
    { "XX...XX", "XX...XX", ".XX.XX.", "..XXX..", ".XX.XX.", "XX...XX", "XX...XX", "......." }, // X
    { "XX..XX.", "XX..XX.", "XX..XX.", ".XXXX..", "..XX...", "..XX...", "..XX...", "......." }, // Y
    { "XXXXXXX", "....XX.", "...XX..", "..XX...", ".XX....", "XX.....", "XXXXXXX", "......." }, // Z
    { ".XXXX..", ".XX....", ".XX....", ".XX....", ".XX....", ".XX....", ".XXXX..", "......." }, // [
    { "XX.....", ".XX....", "..XX...", "...XX..", "....XX.", ".....XX", "......X", "......." }, // backslash
    { ".XXXX..", "...XX..", "...XX..", "...XX..", "...XX..", "...XX..", ".XXXX..", "......." }, // ]
    { "...X...", "..XXX..", ".XX.XX.", "XX...XX", ".......", ".......", ".......", "......." }, // ^
    { ".......", ".......", ".......", ".......", ".......", ".......", ".......", "XXXXXXX" }  // _
};

static const char* const k_bar[8] =
{
    "..XX...", "..XX...", "..XX...", "..XX...", "..XX...", "..XX...", "..XX...", "..XX..."
};

static uint8_t s_font[256][8] SHELL_STATE;

static void convert(uint8_t* glyph, const char* const* art);

void font_init(void)
{
    for (int c = 0; c < 256; c++)
    {
        for (int y = 0; y < 8; y++)
            s_font[c][y] = 0;
    }

    for (int c = FONT_FIRST; c <= FONT_LAST; c++)
        convert(s_font[c], k_glyphs[c - FONT_FIRST]);

    for (int c = 'a'; c <= 'z'; c++)
    {
        for (int y = 0; y < 8; y++)
            s_font[c][y] = s_font[c - 'a' + 'A'][y];
    }

    convert(s_font['|'], k_bar);

    for (int y = 0; y < 8; y++)
        s_font[FONT_BLOCK][y] = 0xFF;
}

const uint8_t* font_glyph(uint8_t c)
{
    return s_font[c];
}

static void convert(uint8_t* glyph, const char* const* art)
{
    for (int y = 0; y < 8; y++)
    {
        uint8_t bits = 0;

        for (int x = 0; x < 7; x++)
        {
            if (art[y][x] == 'X')
                bits |= (uint8_t)(0x80 >> x);
        }

        glyph[y] = bits;
    }
}
