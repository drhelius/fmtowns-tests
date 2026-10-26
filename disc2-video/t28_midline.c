#include "tests.h"
#include "canvas.h"
#include "input.h"
#include "raster.h"

// 2.8 Writes during a line (docs/12, HT-19, HT-58), for photos: every frame a register changes at display line 200
// plus a delay D after the HSYNC rise, and goes back two lines after VDE. Palette entry 1 (the whole single-page
// 256-color screen, with a ruler column every 32 pixels, yellow every 64) turns white through blue, magenta and white
// as its three components land; the other variants flip the priority bit, turn layer 0 off through FDA0h, change the
// layer formats or mask the FM-R planes. A ruler every 10 lines, numbered every 20, shows the line a change lands on.
// Left/Right move D by one pass, Up/Down by ten, A picks the variant in step 3, Select cycles the 15 kHz mode, the
// 31 kHz one at 25.175 MHz and the 31 kHz one at 21.05 MHz (Chase H.Q.'s MODE 2 timing), B ends the step

#define STEPS 3
#define VARIANTS 4
#define TIMINGS 3
#define TIMING_15K 1
#define WRITE_LINE 200
#define TEXT_X 16
#define TEXT_Y 16
#define BASE_RGB 0x000080u
#define WHITE_RGB 0xFFFFFFu
#define FMR_PAGE 0x000CFF82u

typedef struct
{
    uint32_t line_ns[STEPS];
    uint32_t pass_ps;
    int16_t delays[STEPS];
    uint8_t variants[STEPS];
    uint8_t modes[STEPS];
    uint8_t ran[STEPS];
    uint8_t reserved[3];
} midline_t;

typedef struct
{
    const video_mode_t* mode;
    raster_t raster;
    canvas_t canvas;
    canvas_t overlay;
    int write;
    int reset;
} setup_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void run_step(midline_t* result, int step);
static const char* mode_name(int step, int variant, int which);
static void setup_mode(setup_t* setup, int step, int variant, int which);
static int build(const setup_t* setup, int step, int variant, int delay, raster_event_t* events);
static void overlay(const setup_t* setup, int step, int variant, int delay);
static void rulers(const canvas_t* canvas, uint8_t color);

static const char* const k_variant_names[VARIANTS] = { "PRIORITY BIT", "FDA0H LAYER 0 OFF", "0448H FORMATS",
    "CFF82H PLANES, MODE 1" };

static const test_step_t k_steps[] =
{
    { "PALETTE IN A LINE", 0 },
    { "PALETTE, FINE PATTERN", 0 },
    { "OTHER REGISTERS", 0 },
    { 0, 0 }
};

const test_t g_test_midline =
{
    "2.8", "WRITES DURING A LINE", "PHOTOS", 0, 3, sizeof(midline_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    midline_t* result = (midline_t*)data;

    for (int step = 0; step < STEPS; step++)
    {
        if ((steps & (1u << step)) != 0)
            run_step(result, step);
    }

    if ((g_boot.port_0404 & 0x80) == 0)
        MMIO8(FMR_PAGE) = 0x27;
}

static void run_step(midline_t* result, int step)
{
    setup_t setup;
    raster_event_t events[RASTER_CAPACITY];
    int which = TIMING_15K;
    int variant = 0;
    int delay = 0;
    int dirty = 1;

    setup_mode(&setup, step, variant, which);
    result->ran[step] = 1;

    for (;;)
    {
        int limit = (int)(setup.raster.line_ns * 1000u / (setup.raster.pass_ps + 1));
        int count = build(&setup, step, variant, delay, events);

        raster_frame(&setup.raster, events, count);

        if (dirty)
        {
            overlay(&setup, step, variant, delay);
            result->line_ns[step] = setup.raster.line_ns;
            result->pass_ps = setup.raster.pass_ps;
            result->delays[step] = (int16_t)delay;
            result->variants[step] = (uint8_t)variant;
            result->modes[step] = (uint8_t)which;
            dirty = 0;
        }

        int event = input_poll();

        if (event == INPUT_B)
            break;

        if (event == INPUT_LEFT || event == INPUT_RIGHT || event == INPUT_UP || event == INPUT_DOWN)
        {
            int move = event == INPUT_LEFT ? -1 : event == INPUT_RIGHT ? 1 : event == INPUT_UP ? 10 : -10;
            delay = limit > 0 ? (delay + move + limit) % limit : 0;
            dirty = 1;
        }
        else if ((event == INPUT_A && step == 2) || event == INPUT_SELECT)
        {
            variant = event == INPUT_A ? (variant + 1) % VARIANTS : variant;
            which = event == INPUT_SELECT ? (which + 1) % TIMINGS : which;
            setup_mode(&setup, step, variant, which);
            delay = 0;
            dirty = 1;
        }
    }
}

static const char* mode_name(int step, int variant, int which)
{
    static const char* const k_single[TIMINGS] = { "P1", "P3N", "P1C" };
    static const char* const k_double[TIMINGS] = { "P28", "P14", "P28C" };

    if (step < 2)
        return k_single[which];

    return variant == 3 ? "P6" : k_double[which];
}

// Single-page steps: index 1 everywhere, index 2 every 64 pixels, orange between and, in step 2, index 3 on odd
// pixels. Two-layer
// variants: layer 0 red, layer 1 green or clear, the overlay in layer 1
static void setup_mode(setup_t* setup, int step, int variant, int which)
{
    setup->mode = video_find(mode_name(step, variant, which));
    video_program(setup->mode);
    canvas_palettes(setup->mode);

    if (step < 2)
    {
        canvas_set_color(2, 1, 0, 0, 128);
        canvas_set_color(2, 2, 255, 255, 0);
        out8(OUTPUT_ADDRESS, 0x01);
        out8(OUTPUT_DATA, setup->mode->output[1]);
        canvas_layer(&setup->canvas, 12, 0);
        canvas_window(&setup->canvas, setup->mode, 0);
        canvas_clear(&setup->canvas, 1);

        if (step == 1)
        {
            for (int x = 1; x < setup->canvas.width; x += 2)
                canvas_fill(&setup->canvas, x, 0, 1, setup->canvas.height, CANVAS_GRAY);
        }

        for (int x = 0; x < setup->canvas.width; x += 32)
            canvas_fill(&setup->canvas, x, 0, 1, setup->canvas.height, (x & 63) == 0 ? 2 : CANVAS_ORANGE);

        rulers(&setup->canvas, CANVAS_CYAN);
        setup->overlay = setup->canvas;
    }
    else
    {
        int mode = variant == 3 ? 1 : which == TIMING_15K ? 11 : 10;

        canvas_layer(&setup->canvas, mode, 0);
        canvas_window(&setup->canvas, setup->mode, 0);
        canvas_clear(&setup->canvas, variant == 3 ? CANVAS_YELLOW : CANVAS_RED);
        rulers(&setup->canvas, CANVAS_WHITE);
        canvas_fill(&setup->canvas, TEXT_X - 4, TEXT_Y - 4, 248, 76, CANVAS_CLEAR);
        canvas_layer(&setup->overlay, mode, 1);
        canvas_window(&setup->overlay, setup->mode, 1);
        canvas_clear(&setup->overlay, variant == 0 ? CANVAS_GREEN : CANVAS_CLEAR);
        rulers(&setup->overlay, CANVAS_WHITE);

        if (variant == 3 && (g_boot.port_0404 & 0x80) == 0)
            MMIO8(FMR_PAGE) = 0x27;
    }

    raster_setup(&setup->raster, setup->mode);

    int vst1 = (int)video_register(setup->mode, CRTC_VST1);
    setup->write = ((int)video_register(setup->mode, CRTC_VDS0) - vst1) / 2 + WRITE_LINE;
    setup->reset = ((int)video_register(setup->mode, CRTC_VDE0) - vst1) / 2 + 2;
}

// A line every 10 display lines, numbered every 20 at the left and in the middle
static void rulers(const canvas_t* canvas, uint8_t color)
{
    for (int y = 10; y < canvas->visible_height; y += 10)
    {
        canvas_fill(canvas, 0, y, canvas->width, 1, color);

        if (y % 20 == 0)
        {
            canvas_printf(canvas, 2, y + 1, 1, color, CANVAS_BLACK, "%u", y);
            canvas_printf(canvas, canvas->visible_width / 2 + 2, y + 1, 1, color, CANVAS_BLACK, "%u", y);
        }
    }
}

static int build(const setup_t* setup, int step, int variant, int delay, raster_event_t* events)
{
    raster_event_t* change = &events[0];
    raster_event_t* back = &events[1];
    const video_mode_t* mode = setup->mode;

    change->line = (uint16_t)setup->write;
    change->delay = (uint16_t)delay;
    back->line = (uint16_t)setup->reset;
    back->delay = 0;

    if (step < 2)
    {
        change->kind = RASTER_PALETTE;
        change->reg = 1;
        change->address = WHITE_RGB;
        *back = *change;
        back->line = (uint16_t)setup->reset;
        back->delay = 0;
        back->address = BASE_RGB;
        return 2;
    }

    if (variant == 0 || variant == 2)
    {
        uint8_t reg = (uint8_t)(variant == 0 ? 1 : 0);
        uint8_t value = mode->output[reg];

        change->kind = RASTER_OUTPUT;
        change->reg = reg;
        change->value = variant == 0 ? (uint8_t)(value ^ 0x01) : 0x1D;
        *back = *change;
        back->line = (uint16_t)setup->reset;
        back->delay = 0;
        back->value = value;
        return 2;
    }

    if (variant == 1)
    {
        change->kind = RASTER_PORT;
        change->address = VIDEO_STATUS;
        change->value = 0x03;
        *back = *change;
        back->line = (uint16_t)setup->reset;
        back->delay = 0;
        back->value = 0x0F;
        return 2;
    }

    if ((g_boot.port_0404 & 0x80) != 0)
        return 0;

    change->kind = RASTER_MMIO;
    change->address = FMR_PAGE;
    change->value = 0x20;
    *back = *change;
    back->line = (uint16_t)setup->reset;
    back->delay = 0;
    back->value = 0x27;
    return 2;
}

static void overlay(const setup_t* setup, int step, int variant, int delay)
{
    const canvas_t* canvas = &setup->overlay;
    const video_mode_t* mode = setup->mode;
    uint32_t ns = raster_delay_ns(&setup->raster, (uint32_t)delay);
    uint32_t khz = video_dot_khz(mode);
    uint16_t zoom = video_register(mode, CRTC_ZOOM);
    int y = TEXT_Y;

    canvas_fill(canvas, TEXT_X - 4, TEXT_Y - 4, 248, 76, CANVAS_BLACK);
    canvas_printf(canvas, TEXT_X, y, 1, CANVAS_LIGHT, CANVAS_BLACK, "2.8 %s %s", mode->name, step < 2 ?
        k_steps[step].name : k_variant_names[variant]);
    y += 10;
    canvas_printf(canvas, TEXT_X, y, 1, CANVAS_LIGHT, CANVAS_BLACK, "AT LINE %u (DISPLAY 200) + D", setup->write);
    y += 10;
    canvas_printf(canvas, TEXT_X, y, 1, CANVAS_LIGHT, CANVAS_BLACK, "D %u: %u NS, %u DOTS", delay, ns,
        ns * (khz / 100u) / 10000u);
    y += 10;
    canvas_printf(canvas, TEXT_X, y, 1, CANVAS_LIGHT, CANVAS_BLACK, "HDS %u HDE %u HST %u DOTS",
        video_register(mode, CRTC_HDS0), video_register(mode, CRTC_HDE0), video_register(mode, CRTC_HST));
    y += 10;
    canvas_printf(canvas, TEXT_X, y, 1, CANVAS_LIGHT, CANVAS_BLACK, "%u DOTS A PIXEL, PASS %u PS", (zoom & 0x0F) + 1u,
        setup->raster.pass_ps);
    y += 10;
    canvas_printf(canvas, TEXT_X, y, 1, CANVAS_LIGHT, CANVAS_BLACK, "WROTE AT %u.%u LINES", setup->raster.times[0] *
        1000u / setup->raster.line_ns, setup->raster.times[0] * 10000u / setup->raster.line_ns % 10);
    y += 10;

    uint32_t line = setup->raster.times[0] * 1000u / setup->raster.line_ns;
    uint32_t into = setup->raster.times[0] * 1000u - line * setup->raster.line_ns;
    canvas_printf(canvas, TEXT_X, y, 1, CANVAS_LIGHT, CANVAS_BLACK, "= LINE %u + %u.%u US", line, into / 1000u,
        into / 100u % 10u);
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 1;
}

static void page(const void* data, uint32_t steps, int number)
{
    const midline_t* result = (const midline_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;
    (void)number;

    shell_line(row++, COLOR_CYAN, "THE PHOTOS ARE THE RESULT");
    shell_line(row++, COLOR_WHITE, "SPIN PASS %u PS", result->pass_ps);
    row++;

    for (int step = 0; step < STEPS; step++)
    {
        if (result->ran[step])
            shell_line(row++, COLOR_WHITE, "%u %-4s LINE %u NS, LAST D %d", step + 1, mode_name(step,
                result->variants[step], result->modes[step]), result->line_ns[step], result->delays[step]);
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const midline_t* result = (const midline_t*)data;
    int ran = 0;
    (void)steps;

    for (int step = 0; step < STEPS; step++)
        ran += result->ran[step];

    fmt_format(line, 34, "%u STEPS SHOWN", ran);
    *color = COLOR_WHITE;
}
