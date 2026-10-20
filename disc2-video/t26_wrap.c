#include "tests.h"
#include "canvas.h"
#include "input.h"

// 2.6 Horizontal wrap with CEN = 0 (docs/12, HT-1), for photos: every row of layer 0 starts with a block in a color
// picked by row mod 4, and FA0 starts the display so the row ends 64 or 128 pixels before the right edge; what the
// counter reads after the row end shows in those last columns. A 16-pixel stripe at the left edge has the line's own
// row color. The overlay (layer 1, or rows of layer 0 in one-surface modes) holds the key. Select switches between
// the 31 kHz mode and its 15 kHz counterpart, A the vertical zoom (1x, 2x, 4x), B ends the step

#define STEPS 5
#define TEXT_Y 24
#define ZOOMS 3

typedef struct
{
    const char* modes[2];
    uint8_t layers[2][2];
    uint8_t carry;
} wrap_step_t;

typedef struct
{
    uint8_t ran[STEPS];
    uint8_t last_mode[STEPS];
    uint8_t last_zoom[STEPS];
    uint8_t reserved;
    uint16_t fa[STEPS][2];
} wrap_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static uint16_t draw(int step, int which, int zoom);
static void key(const canvas_t* canvas, int x, int y, int step, const video_mode_t* mode, int marker, uint16_t fa);

static const wrap_step_t k_wrap_steps[STEPS] =
{
    { { "P9", "P9N" }, { { 3, 3 }, { 3, 3 } }, 0 },
    { { "P9", "P9N" }, { { 3, 3 }, { 3, 3 } }, 1 },
    { { "P28", "P14" }, { { 10, 10 }, { 11, 11 } }, 0 },
    { { "P20", "P18" }, { { 5, 5 }, { 8, 8 } }, 0 },
    { { "P1", "P3N" }, { { 12, 0 }, { 14, 0 } }, 0 }
};

static const uint8_t k_markers[4] = { CANVAS_RED, CANVAS_GREEN, CANVAS_BLUE, CANVAS_YELLOW };

static const test_step_t k_steps[] =
{
    { "CEN0 = 0, 4 BPP", 0 },
    { "CEN0 = 1, THE CARRY", 0 },
    { "MODES 10/11, OWN ROW", 0 },
    { "16 BPP", 0 },
    { "8 BPP, ONE SURFACE", 0 },
    { 0, 0 }
};

const test_t g_test_wrap =
{
    "2.6", "HORIZONTAL WRAP, CEN 0", "PHOTOS", 0, 1, sizeof(wrap_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    wrap_t* result = (wrap_t*)data;

    for (int step = 0; step < STEPS; step++)
    {
        int which = 1;
        int zoom = 0;

        if ((steps & (1u << step)) == 0)
            continue;

        result->ran[step] = 1;
        result->fa[step][which] = draw(step, which, zoom);

        for (;;)
        {
            int event = input_poll();

            if (event == INPUT_B)
                break;

            if (event == INPUT_SELECT)
                which ^= 1;
            else if (event == INPUT_A)
                zoom = (zoom + 1) % ZOOMS;
            else
                continue;

            result->fa[step][which] = draw(step, which, zoom);
            result->last_mode[step] = (uint8_t)which;
            result->last_zoom[step] = (uint8_t)zoom;
        }
    }
}

// Rows of layer 0 and the overlay; FA0 puts the row end 64 or 128 pixels before the window's right edge
static uint16_t draw(int step, int which, int zoom)
{
    const wrap_step_t* entry = &k_wrap_steps[step];
    const video_mode_t* mode = video_find(entry->modes[which]);
    int two = entry->layers[which][1] != 0;
    canvas_t canvas;
    canvas_t overlay;

    video_program(mode);
    canvas_palettes(mode);
    canvas_layer(&canvas, entry->layers[which][0], 0);
    canvas_window(&canvas, mode, 0);

    int width = canvas.width;
    int visible = canvas.visible_width < width ? canvas.visible_width : width;
    int marker = visible >= 512 ? 128 : 64;
    int start = width - (visible - marker);
    int unit = canvas.bpp == 16 ? (two ? 2 : 4) : 8;
    uint16_t fa = (uint16_t)(start / unit);
    uint8_t neutral = two ? CANVAS_CLEAR : CANVAS_DARK;

    canvas_clear(&canvas, neutral);

    for (int y = 0; y < canvas.height; y++)
    {
        canvas_fill(&canvas, 0, y, marker, 1, k_markers[y & 3]);
        canvas_fill(&canvas, start, y, 16, 1, k_markers[y & 3]);
    }

    if (two)
    {
        canvas_layer(&overlay, entry->layers[which][1], 1);
        canvas_clear(&overlay, CANVAS_CLEAR);
        key(&overlay, 16, TEXT_Y, step, mode, marker, fa);
    }
    else
        key(&canvas, start + 24, TEXT_Y, step, mode, marker, fa);

    uint16_t cr0 = video_register(mode, CRTC_CR0);
    uint16_t zoom_value = (uint16_t)(video_register(mode, CRTC_ZOOM) & ~0x00F0);

    video_crtc(CRTC_CR0, (uint16_t)((entry->carry ? cr0 | 0x0010 : cr0 & ~0x0010) | CRTC_START));
    video_crtc(CRTC_ZOOM, (uint16_t)(zoom_value | ((zoom == 0 ? 0 : zoom == 1 ? 1 : 3) << 4)));
    video_crtc(CRTC_FA0, fa);
    return fa;
}

static void key(const canvas_t* canvas, int x, int y, int step, const video_mode_t* mode, int marker, uint16_t fa)
{
    canvas_fill(canvas, x - 4, y - 4, 248, 132, CANVAS_BLACK);
    canvas_printf(canvas, x, y, 1, CANVAS_CYAN, CANVAS_BLACK, "2.6 %s, %s", k_steps[step].name, mode->name);
    y += 10;
    canvas_printf(canvas, x, y, 1, CANVAS_WHITE, CANVAS_BLACK, "FA0 %XH, ROW ENDS %u PX FROM", fa, marker);
    y += 10;
    canvas_text(canvas, x, y, 1, CANVAS_WHITE, CANVAS_BLACK, "THE RIGHT EDGE; LEFT STRIPE IS");
    y += 10;
    canvas_text(canvas, x, y, 1, CANVAS_WHITE, CANVAS_BLACK, "THE LINE'S OWN ROW");
    y += 12;

    int left = x + canvas_text(canvas, x, y, 1, CANVAS_WHITE, CANVAS_BLACK, "ROW MOD 4: ");

    for (int i = 0; i < 4; i++)
        left += canvas_printf(canvas, left, y, 1, k_markers[i], CANVAS_BLACK, "%u ", i);

    y += 12;
    canvas_text(canvas, x, y, 1, CANVAS_WHITE, CANVAS_BLACK, "RIGHT BLOCK FOR ROWS 0,1,2,3:");
    y += 10;
    canvas_text(canvas, x, y, 1, CANVAS_GRAY, CANVAS_BLACK, "1,0,3,2 8-BIT COUNTER (BOOK)");
    y += 10;
    canvas_text(canvas, x, y, 1, CANVAS_GRAY, CANVAS_BLACK, "0,1,2,3 EACH ROW ON ITSELF");
    y += 10;
    canvas_text(canvas, x, y, 1, CANVAS_GRAY, CANVAS_BLACK, "1,2,3,0 CARRY, AS CEN0 = 1");
    y += 12;
    canvas_text(canvas, x, y, 1, CANVAS_GRAY, CANVAS_BLACK, "SEL 31K/15K, A ZOOM, B END");
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 1;
}

static void page(const void* data, uint32_t steps, int number)
{
    const wrap_t* result = (const wrap_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;
    (void)number;

    shell_line(row++, COLOR_CYAN, "THE PHOTOS ARE THE RESULT");
    row++;

    for (int step = 0; step < STEPS; step++)
    {
        if (result->ran[step])
            shell_line(row++, COLOR_WHITE, "%u %-20s FA0 %XH/%XH", step + 1, k_steps[step].name, result->fa[step][0],
                result->fa[step][1]);
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const wrap_t* result = (const wrap_t*)data;
    int ran = 0;
    (void)steps;

    for (int step = 0; step < STEPS; step++)
        ran += result->ran[step];

    fmt_format(line, 34, "%u STEPS SHOWN", ran);
    *color = COLOR_WHITE;
}
