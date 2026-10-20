#include "tests.h"
#include "canvas.h"
#include "input.h"

// 2.2 Display mode cards (docs/12, HT-44): a card per preset for photos. Each layer gets a border at the size its
// CRTC window, zoom and interlace give and one 8 pixels in, its label drawn in the mode, and either line stripes (line
// color = line mod 4, numbered every 16 lines) or a grid every 16 pixels with rulers; layer 0 counts frames and leaves
// a hole for layer 1's label. Left/Right step, A toggles the view, Run stops and restarts the CRTC (START) for a photo
// of a stopped display, B returns to the shell

#define CARD_CAPACITY 48
#define VIEW_STRIPES 0
#define VIEW_GRID 1

typedef struct
{
    const char* name;
    uint8_t modes[2];
} card_t;

typedef struct
{
    uint8_t viewed[CARD_CAPACITY];
    uint8_t card_count;
    uint8_t last;
    uint16_t reserved;
    uint32_t frames;
} cards_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void draw_card(const card_t* card, int view, canvas_t* first);
static void draw_layer(const canvas_t* canvas, const card_t* card, int layer, int view);
static const char* line_rate(const video_mode_t* mode);

// Table 4.7.3's mode pairs, then the extra modes of the table in video.c
static const card_t k_cards[] =
{
    { "P1", { 12, 0 } }, { "P2", { 13, 0 } }, { "P3", { 14, 0 } }, { "P4", { 15, 0 } }, { "P5", { 16, 0 } },
    { "P6", { 1, 1 } }, { "P8", { 2, 2 } }, { "P9", { 3, 3 } }, { "P10", { 4, 4 } }, { "P11", { 6, 4 } },
    { "P12", { 9, 9 } }, { "P13", { 7, 9 } }, { "P14", { 11, 11 } }, { "P15", { 8, 11 } }, { "P16", { 6, 6 } },
    { "P17", { 7, 7 } }, { "P18", { 8, 8 } }, { "P19", { 5, 3 } }, { "P20", { 5, 5 } }, { "P22", { 3, 5 } },
    { "P23", { 4, 6 } }, { "P24", { 9, 7 } }, { "P25", { 11, 8 } }, { "P26", { 3, 10 } }, { "P27", { 10, 3 } },
    { "P28", { 10, 10 } }, { "P29", { 10, 5 } }, { "P30", { 5, 10 } }, { "P31", { 17, 0 } }, { "P32", { 18, 0 } },
    { "R50", { 1, 4 } }, { "CAM", { 11, 8 } }, { "LS", { 10, 5 } }, { "P3N", { 14, 0 } }, { "P5N", { 16, 0 } },
    { "P9N", { 3, 3 } }, { "S15", { 11, 11 } }, { "S31", { 10, 10 } }
};

static const uint8_t k_stripes[4] = { CANVAS_RED, CANVAS_GREEN, CANVAS_BLUE, CANVAS_WHITE };

const test_t g_test_cards =
{
    "2.2", "DISPLAY MODE CARDS", "PHOTOS", 0, 1, sizeof(cards_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    cards_t* result = (cards_t*)data;
    int count = (int)COUNT_OF(k_cards);
    int index = 0;
    int view = VIEW_STRIPES;
    int stopped = 0;
    uint32_t frame = 0;
    uint32_t time;
    canvas_t canvas;
    (void)steps;

    result->card_count = (uint8_t)count;
    draw_card(&k_cards[index], view, &canvas);
    result->viewed[index] = 1;

    for (;;)
    {
        video_vsync_rise(&time);
        frame++;
        canvas_printf(&canvas, 48, 48, 1, CANVAS_WHITE, CANVAS_BLACK, "FRAME %u", frame);

        int event = input_poll();

        if (event == INPUT_B)
            break;

        if (event == INPUT_LEFT || event == INPUT_RIGHT)
            index = (index + (event == INPUT_RIGHT ? 1 : count - 1)) % count;
        else if (event == INPUT_A)
            view ^= 1;
        else if (event == INPUT_RUN)
        {
            uint16_t cr0 = video_register(video_find(k_cards[index].name), CRTC_CR0);

            stopped ^= 1;
            video_crtc(CRTC_CR0, stopped ? (uint16_t)(cr0 & ~CRTC_START) : (uint16_t)(cr0 | CRTC_START));
            continue;
        }
        else
            continue;

        stopped = 0;

        result->frames += frame;
        frame = 0;
        draw_card(&k_cards[index], view, &canvas);
        result->viewed[index] = 1;
        result->last = (uint8_t)index;
    }

    result->frames += frame;
}

static void draw_card(const card_t* card, int view, canvas_t* first)
{
    const video_mode_t* mode = video_find(card->name);
    int layers = card->modes[1] != 0 ? 2 : 1;

    video_program(mode);
    canvas_palettes(mode);

    for (int layer = 0; layer < layers; layer++)
    {
        canvas_t canvas;
        canvas_layer(&canvas, card->modes[layer], layer);
        canvas_window(&canvas, mode, layer);
        draw_layer(&canvas, card, layer, view);

        if (layer == 0)
            *first = canvas;
    }
}

static void draw_layer(const canvas_t* canvas, const card_t* card, int layer, int view)
{
    const video_mode_t* mode = video_find(card->name);
    int width = canvas->visible_width;
    int height = canvas->visible_height;
    uint8_t edge = layer == 0 ? CANVAS_WHITE : CANVAS_YELLOW;

    if (layer != 0)
        canvas_clear(canvas, CANVAS_CLEAR);
    else if (view == VIEW_STRIPES)
    {
        for (int y = 0; y < canvas->height; y++)
            canvas_fill(canvas, 0, y, canvas->width, 1, k_stripes[y & 3]);

        for (int y = 0; y < canvas->height; y += 16)
            canvas_printf(canvas, 12, y, 1, CANVAS_BLACK, CANVAS_WHITE, "%u", y);
    }
    else
    {
        canvas_clear(canvas, CANVAS_BLACK);

        for (int x = 0; x < canvas->width; x += 16)
            canvas_fill(canvas, x, 0, 1, canvas->height, (x & 63) == 0 ? CANVAS_GRAY : CANVAS_DARK);

        for (int y = 0; y < canvas->height; y += 16)
            canvas_fill(canvas, 0, y, canvas->width, 1, (y & 63) == 0 ? CANVAS_GRAY : CANVAS_DARK);

        for (int x = 64; x < canvas->width; x += 64)
            canvas_printf(canvas, x + 2, 2, 1, CANVAS_CYAN, CANVAS_BLACK, "%u", x);

        for (int y = 16; y < canvas->height; y += 16)
            canvas_printf(canvas, 2, y + 4, 1, CANVAS_CYAN, CANVAS_BLACK, "%u", y);
    }

    canvas_frame(canvas, 0, 0, width, height, edge);
    canvas_frame(canvas, 8, 8, width - 16, height - 16, layer == 0 ? CANVAS_GRAY : CANVAS_ORANGE);

    if (layer == 0)
    {
        if (card->modes[1] != 0)
            canvas_printf(canvas, 48, 16, 1, CANVAS_WHITE, CANVAS_BLACK, "%s MODES %u+%u %s", card->name,
                card->modes[0], card->modes[1], line_rate(mode));
        else
            canvas_printf(canvas, 48, 16, 1, CANVAS_WHITE, CANVAS_BLACK, "%s MODE %u %s", card->name, card->modes[0],
                line_rate(mode));

        canvas_printf(canvas, 48, 24, 1, CANVAS_WHITE, CANVAS_BLACK, "L0 %uX%u %u BPP", width, height, canvas->bpp);

        if (card->modes[1] != 0)
            canvas_fill(canvas, 48, 32, 136, 8, CANVAS_CLEAR);
    }
    else
        canvas_printf(canvas, 48, 32, 1, CANVAS_YELLOW, CANVAS_BLACK, "L1 %uX%u %u BPP", width, height, canvas->bpp);
}

static const char* line_rate(const video_mode_t* mode)
{
    uint32_t line = video_line_ns(mode);
    return line < 36000 ? "31K" : line < 46000 ? "24K" : "15K";
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 1;
}

static void page(const void* data, uint32_t steps, int number)
{
    const cards_t* result = (const cards_t*)data;
    int row = SHELL_BODY_TOP;
    int viewed = 0;
    char line[40];
    int length = 0;
    (void)steps;
    (void)number;

    for (int i = 0; i < result->card_count && i < CARD_CAPACITY; i++)
        viewed += result->viewed[i] != 0;

    shell_line(row++, COLOR_CYAN, "%u OF %u CARDS SHOWN, %u FRAMES", viewed, result->card_count, result->frames);
    shell_line(row++, COLOR_GRAY, "THE PHOTOS ARE THE RESULT");
    row++;

    for (int i = 0; i < result->card_count && i < CARD_CAPACITY; i++)
    {
        if (result->viewed[i] == 0)
            continue;

        if (length > 32)
        {
            shell_line(row++, COLOR_WHITE, "%s", line);
            length = 0;
        }

        length += fmt_format(line + length, (int)sizeof(line) - length, "%s ", k_cards[i].name);
    }

    if (length != 0)
        shell_line(row++, COLOR_WHITE, "%s", line);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const cards_t* result = (const cards_t*)data;
    int viewed = 0;
    (void)steps;

    for (int i = 0; i < result->card_count && i < CARD_CAPACITY; i++)
        viewed += result->viewed[i] != 0;

    fmt_format(line, 34, "%u CARDS SHOWN", viewed);
    *color = COLOR_WHITE;
}
