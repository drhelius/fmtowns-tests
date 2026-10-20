#include "tests.h"
#include "canvas.h"
#include "input.h"

// 2.7 FM-R display page (docs/12, HT-15), for photos: VRAM filled through the two-page view so every byte (every
// pixel in 32,768 colors) shows its 64 KiB region, the two-page offset >> 16, in one of eight colors; then CFF82h
// bit 4 (67h for page 0, 77h for page 1). A legend at the bottom names the colors. Mode 1 also writes P0 ROW n and
// P1 ROW n into its two pages, and the sprite step labels the two halves of layer 1. A flips the page bit, Select
// switches between the 31 kHz (24 kHz for mode 1) and the 15 kHz mode, Run switches SPEN in the sprite step, B ends

#define STEPS 5
#define FMR_PAGE 0x000CFF82u
#define PAGE_0 0x67
#define PAGE_1 0x77
#define VRAM_BYTES 0x80000u
#define REGIONS 8
#define FORMAT_BYTES 0
#define FORMAT_NIBBLES 1
#define FORMAT_WORDS 2
#define INDEX_BLACK 8
#define INDEX_WHITE 9

typedef struct
{
    uint8_t ran[STEPS];
    uint8_t last_page[STEPS];
    uint8_t last_mode[STEPS];
    uint8_t mapped;
    uint8_t reserved[2];
} page_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static const char* mode_name(int step, int which);
static void draw(int step, int which, int sprites);
static void fill_regions(int format);
static void legend(const canvas_t* canvas, int step, const char* mode);

static const uint8_t k_region_colors[REGIONS] =
{
    CANVAS_GRAY, CANVAS_RED, CANVAS_GREEN, CANVAS_BLUE, CANVAS_YELLOW, CANVAS_CYAN, CANVAS_MAGENTA, CANVAS_WHITE
};

static const test_step_t k_steps[] =
{
    { "256 COLORS, PAGE 0", 0 },
    { "256 COLORS, PAGE 1", 0 },
    { "32K COLORS", 0 },
    { "MODE 1, TWO PAGES", 0 },
    { "SPRITE LAYER", 0 },
    { 0, 0 }
};

const test_t g_test_page =
{
    "2.7", "FM-R DISPLAY PAGE", "PHOTOS", 0, 1, sizeof(page_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    page_t* result = (page_t*)data;

    result->mapped = (g_boot.port_0404 & 0x80) == 0;

    for (int step = 0; step < STEPS; step++)
    {
        int which = step == 3 ? 0 : 1;
        int bit = step == 1;
        int sprites = 0;

        if ((steps & (1u << step)) == 0)
            continue;

        result->ran[step] = 1;
        draw(step, which, sprites);

        for (;;)
        {
            if (result->mapped)
                MMIO8(FMR_PAGE) = bit ? PAGE_1 : PAGE_0;

            result->last_page[step] = (uint8_t)bit;
            result->last_mode[step] = (uint8_t)which;

            int event;

            while ((event = input_poll()) == INPUT_NONE)
                ;

            if (event == INPUT_B)
                break;

            if (event == INPUT_A && step >= 2)
                bit ^= 1;
            else if (event == INPUT_SELECT && step != 3)
            {
                which ^= 1;
                draw(step, which, sprites);
            }
            else if (event == INPUT_RUN && step == 4)
            {
                sprites ^= 1;
                draw(step, which, sprites);
            }
        }

        if (result->mapped)
            MMIO8(FMR_PAGE) = 0x27;
    }
}

static const char* mode_name(int step, int which)
{
    static const char* const k_names[STEPS][2] =
    {
        { "P1", "P3N" }, { "P1", "P3N" }, { "P4", "P5N" }, { "P6", "P6" }, { "P20", "P18" }
    };

    return k_names[step][which];
}

static void draw(int step, int which, int sprites)
{
    const video_mode_t* mode = video_find(mode_name(step, which));
    canvas_t canvas;

    if ((g_boot.port_0404 & 0x80) == 0)
        MMIO8(FMR_PAGE) = PAGE_0;

    out8(0x0450, 0x01);
    out8(0x0452, 0x00);
    video_program(mode);
    canvas_palettes(mode);

    if (step < 3)
    {
        fill_regions(step == 2 ? FORMAT_WORDS : FORMAT_BYTES);
        canvas_layer(&canvas, step == 2 ? 16 : 12, 0);
        canvas_window(&canvas, mode, 0);
        legend(&canvas, step, mode->name);
        return;
    }

    if (step == 3)
    {
        fill_regions(FORMAT_NIBBLES);
        canvas_layer(&canvas, 1, 1);
        canvas_clear(&canvas, CANVAS_CLEAR);
        canvas_layer(&canvas, 1, 1);
        canvas.base += 0x20000u;
        canvas_clear(&canvas, CANVAS_CLEAR);

        for (int half = 0; half < 2; half++)
        {
            canvas_layer(&canvas, 1, 0);
            canvas.base += half * 0x20000u;

            for (int y = 0; y < canvas.height; y += 16)
                canvas_printf(&canvas, 8 + half * 128, y, 1, INDEX_WHITE, INDEX_BLACK, "P%u ROW %u", half, y);

            canvas.visible_height = 400;
            legend(&canvas, step, mode->name);
        }

        return;
    }

    canvas_layer(&canvas, which ? 8 : 5, 0);
    canvas_window(&canvas, mode, 0);
    canvas_clear(&canvas, CANVAS_CLEAR);
    legend(&canvas, step, mode->name);

    canvas_layer(&canvas, which ? 8 : 5, 1);

    for (int half = 0; half < 2; half++)
    {
        canvas_fill(&canvas, 0, half * 256, 256, 256, CANVAS_CLEAR);
        canvas_frame(&canvas, 32, half * 256 + 64, 192, 96, half ? CANVAS_CYAN : CANVAS_ORANGE);
        canvas_printf(&canvas, 48, half * 256 + 96, 3, half ? CANVAS_CYAN : CANVAS_ORANGE, CANVAS_BLACK, "HALF %u",
            half);
    }

    volatile uint16_t* entry = (volatile uint16_t*)g_machine.sprite_ram + 1023 * 4;
    volatile uint16_t* pattern = (volatile uint16_t*)g_machine.sprite_ram + 0x84 * 64;

    for (int i = 0; i < 64; i++)
        pattern[i] = 0xFFFF;

    ((volatile uint16_t*)g_machine.sprite_ram)[0x100 * 16 + 15] = 0x7FFF;

    entry[0] = 120;
    entry[1] = 40;
    entry[2] = 0x84;
    entry[3] = 0x8100;
    out8(0x0450, 0x00);
    out8(0x0452, 0xFF);
    out8(0x0450, 0x01);
    out8(0x0452, (uint8_t)(sprites ? 0x83 : 0x03));
}

// Two-page view offset >> 16 in every byte (FORMAT_BYTES), nibble (FORMAT_NIBBLES), or 16-bit pixel as one of eight
// colors (FORMAT_WORDS)
static void fill_regions(int format)
{
    canvas_t canvas;

    canvas_layer(&canvas, 5, 0);

    for (uint32_t offset = 0; offset < VRAM_BYTES; offset += 4)
    {
        uint32_t region = offset >> 16;
        uint32_t value = format == FORMAT_WORDS ? canvas_value(&canvas, k_region_colors[region]) * 0x00010001u :
            region * (format == FORMAT_NIBBLES ? 0x11111111u : 0x01010101u);

        MMIO32(g_machine.vram + offset) = value;
    }

    for (int i = 0; i < REGIONS; i++)
    {
        static const uint8_t k_rgb[REGIONS][3] =
        {
            { 128, 128, 128 }, { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 0 }, { 0, 255, 255 },
            { 255, 0, 255 }, { 255, 255, 255 }
        };

        for (int bank = 0; bank < 3; bank++)
            canvas_set_color(bank, (uint8_t)i, k_rgb[i][0], k_rgb[i][1], k_rgb[i][2]);
    }

    for (int bank = 0; bank < 3; bank++)
    {
        canvas_set_color(bank, INDEX_BLACK, 0, 0, 0);
        canvas_set_color(bank, INDEX_WHITE, 255, 255, 255);
    }
}

// The eight region colors with their numbers, at the bottom of the window; indexed layers use the region palette
// entries and black and white at 8 and 9
static void legend(const canvas_t* canvas, int step, const char* mode)
{
    int y = canvas->visible_height - 28;
    int width = canvas->visible_width < 256 ? 24 : 32;
    int indexed = canvas->bpp != 16;
    uint8_t black = indexed ? INDEX_BLACK : CANVAS_BLACK;

    canvas_fill(canvas, 0, y - 2, width * REGIONS + 8, 30, black);
    canvas_printf(canvas, 4, y, 1, indexed ? INDEX_WHITE : CANVAS_WHITE, black, "2.7 %s %s", mode,
        k_steps[step].name);

    for (int i = 0; i < REGIONS; i++)
    {
        uint8_t color = indexed ? (uint8_t)i : k_region_colors[i];
        canvas_fill(canvas, 4 + i * width, y + 10, width - 4, 14, color);
        canvas_printf(canvas, 4 + i * width + 4, y + 13, 1, black, color, "%u", i);
    }
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 1;
}

static void page(const void* data, uint32_t steps, int number)
{
    const page_t* result = (const page_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;
    (void)number;

    shell_line(row++, COLOR_CYAN, "THE PHOTOS ARE THE RESULT");

    if (!result->mapped)
        shell_line(row++, COLOR_YELLOW, "0404H BIT 7 SET: NO CFF82H");

    row++;

    for (int step = 0; step < STEPS; step++)
    {
        if (result->ran[step])
            shell_line(row++, COLOR_WHITE, "%u %-19s %s PAGE %u", step + 1, k_steps[step].name,
                mode_name(step, result->last_mode[step]), result->last_page[step]);
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const page_t* result = (const page_t*)data;
    int ran = 0;
    (void)steps;

    for (int step = 0; step < STEPS; step++)
        ran += result->ran[step];

    fmt_format(line, 34, "%u STEPS SHOWN", ran);
    *color = result->mapped ? COLOR_WHITE : COLOR_YELLOW;
}
