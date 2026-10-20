#include "tests.h"
#include "canvas.h"
#include "input.h"
#include "raster.h"

// 2.5 Line start address (docs/12, HT-16), for photos: single-page 256-color modes with 1 KiB rows (P1 at 31 kHz,
// P3N at 15 kHz, P3 interlaced), every VRAM row showing its number as nine bars (bits 8-0, white for 1). Each step
// writes CRTC registers at set lines of every frame and puts them back at VDE + 2 lines (the reset); the overlay in
// the rows from 120 shows what was written where. Left/Right move by one, Up/Down by ten, A picks the variant, Select
// switches between P1 and P3N, B ends the step

#define STEPS 7
#define MODE_P1 0
#define MODE_P3N 1
#define MODE_P3 2
#define MODES 3
#define ROWS 512
#define BAR_WIDTH 32
#define BARS 9
#define LABEL_X 296
#define TEXT_X 344
#define TEXT_Y 120
#define TEXT_LINES 12
#define WRITE_LINE 200
#define FA_ROW_100 0x3200

typedef struct
{
    uint32_t line_ns[MODES];
    uint32_t pass_ps;
    uint16_t frame_lines[MODES];
    int16_t params[STEPS];
    uint8_t variants[STEPS];
    uint8_t modes[STEPS];
    uint8_t ran[STEPS];
    uint8_t reserved;
} lines_t;

typedef struct
{
    int index;
    const video_mode_t* mode;
    raster_t raster;
    canvas_t canvas;
    int vsync_end;
    int vds;
    int vde;
    int reset;
    int frames;
} setup_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void run_step(lines_t* result, int step);
static void setup_mode(setup_t* setup, int index, int step);
static int variants(int step);
static int build(const setup_t* setup, int step, int variant, int param, raster_event_t* events);
static void add(raster_event_t* events, int* count, int line, int delay, uint8_t kind, uint8_t reg, uint16_t value);
static void overlay(const setup_t* setup, int step, int variant, int param);
static int lines_of(const setup_t* setup, int half_lines);
static void restore(const setup_t* setup, int step);
static void written(const setup_t* setup, const raster_event_t* events, int count);

static const char* const k_mode_names[MODES] = { "P1", "P3N", "P3" };

static const test_step_t k_steps[] =
{
    { "FA LOAD POINT", 0 },
    { "LO AT LINE 200", 0 },
    { "LO AND HAJ INSIDE A LINE", 0 },
    { "ZOOM ON A ROW PAIR", 0 },
    { "FA IN TWO HALVES", 0 },
    { "VDE ABOVE THE LINE", 0 },
    { "INTERLACED P3", 0 },
    { 0, 0 }
};

const test_t g_test_lines =
{
    "2.5", "LINE START ADDRESS", "PHOTOS", 0, 1, sizeof(lines_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    lines_t* result = (lines_t*)data;

    for (int step = 0; step < STEPS; step++)
    {
        if ((steps & (1u << step)) != 0)
            run_step(result, step);
    }
}

static void run_step(lines_t* result, int step)
{
    setup_t setup;
    raster_event_t events[RASTER_CAPACITY];
    int index = step == 6 ? MODE_P3 : MODE_P3N;
    int variant = 0;
    int param = 0;
    int dirty = 1;

    setup_mode(&setup, index, step);
    result->ran[step] = 1;

    for (;;)
    {
        int limit = step == 2 ? (int)(setup.raster.line_ns * 1000u / (setup.raster.pass_ps + 1)) : setup.frames;
        int count = build(&setup, step, variant, param, events);

        raster_frame(&setup.raster, events, count);

        if (dirty)
        {
            overlay(&setup, step, variant, param);
            written(&setup, events, count);
            result->line_ns[index] = setup.raster.line_ns;
            result->frame_lines[index] = (uint16_t)setup.frames;
            result->pass_ps = setup.raster.pass_ps;
            result->params[step] = (int16_t)param;
            result->variants[step] = (uint8_t)variant;
            result->modes[step] = (uint8_t)index;
            dirty = 0;
        }

        int event = input_poll();

        if (event == INPUT_B)
            break;

        if (event == INPUT_LEFT || event == INPUT_RIGHT || event == INPUT_UP || event == INPUT_DOWN)
        {
            int move = event == INPUT_LEFT ? -1 : event == INPUT_RIGHT ? 1 : event == INPUT_UP ? 10 : -10;
            param = limit > 0 ? (param + move + limit) % limit : 0;
            dirty = 1;
        }
        else if (event == INPUT_A)
        {
            variant = (variant + 1) % variants(step);
            param = 0;
            dirty = 1;
            restore(&setup, step);
        }
        else if (event == INPUT_SELECT && step != 6)
        {
            index = index == MODE_P1 ? MODE_P3N : MODE_P1;
            setup_mode(&setup, index, step);
            param = 0;
            dirty = 1;
        }
    }
}

// The mode, its rows, its measured lines, and where VSYNC, VDS, VDE and the reset fall from the VSYNC rise
static void setup_mode(setup_t* setup, int index, int step)
{
    setup->index = index;
    setup->mode = video_find(k_mode_names[index]);
    video_program(setup->mode);
    canvas_palettes(setup->mode);
    canvas_layer(&setup->canvas, 12, 0);
    canvas_clear(&setup->canvas, CANVAS_BLACK);

    for (int row = 0; row < ROWS; row++)
    {
        for (int bit = 0; bit < BARS; bit++)
        {
            uint8_t color = (row & (1 << (BARS - 1 - bit))) != 0 ? CANVAS_WHITE : CANVAS_DARK;
            canvas_fill(&setup->canvas, bit * BAR_WIDTH, row, BAR_WIDTH - 2, 1, color);
        }

        if ((row & 15) == 0)
            canvas_printf(&setup->canvas, LABEL_X, row, 1, CANVAS_CYAN, CANVAS_BLACK, "%u", row);
    }

    restore(setup, step);
    raster_setup(&setup->raster, setup->mode);
    setup->vsync_end = lines_of(setup, video_register(setup->mode, CRTC_VST2));
    setup->vds = lines_of(setup, video_register(setup->mode, CRTC_VDS0));
    setup->vde = lines_of(setup, video_register(setup->mode, CRTC_VDE0));
    setup->reset = setup->vde + 2;
    setup->frames = setup->raster.frame_lines != 0 ? (int)setup->raster.frame_lines : 262;
}

// The mode's registers again, so nothing a variant wrote carries over; step 4 shows rows twice
static void restore(const setup_t* setup, int step)
{
    video_program(setup->mode);

    if (step == 3)
        video_crtc(CRTC_ZOOM, (uint16_t)(video_register(setup->mode, CRTC_ZOOM) | 0x0010));
}

// Lines from the VSYNC rise to a vertical counter value in half-lines
static int lines_of(const setup_t* setup, int half_lines)
{
    return (half_lines - (int)video_register(setup->mode, CRTC_VST1)) / 2;
}

static int variants(int step)
{
    return step == 2 ? 2 : step == 3 ? 2 : step == 6 ? 3 : 1;
}

// Kept in line order, a later event on the same line after an earlier one
static void add(raster_event_t* events, int* count, int line, int delay, uint8_t kind, uint8_t reg, uint16_t value)
{
    int i = *count;

    while (i > 0 && events[i - 1].line > line)
    {
        events[i] = events[i - 1];
        i--;
    }

    events[i].line = (uint16_t)line;
    events[i].delay = (uint16_t)delay;
    events[i].kind = kind;
    events[i].reg = reg;
    events[i].value = value;
    events[i].address = 0;
    (*count)++;
}

static int build(const setup_t* setup, int step, int variant, int param, raster_event_t* events)
{
    const video_mode_t* mode = setup->mode;
    int count = 0;
    int last = setup->frames - 2;
    int write = setup->vds + WRITE_LINE;
    int moved = (setup->reset + param) % setup->frames;
    uint16_t lo = video_register(mode, CRTC_LO0);

    moved = moved > last ? last : moved;
    write = write > last ? last : write;

    if (step == 0 || (step == 6 && variant == 0))
    {
        add(events, &count, setup->reset, 0, RASTER_CRTC, CRTC_FA0, 0);
        add(events, &count, moved, 0, RASTER_CRTC, CRTC_FA0, FA_ROW_100);
    }
    else if (step == 1 || (step == 6 && variant == 1))
    {
        add(events, &count, write, 0, RASTER_CRTC, CRTC_LO0, (uint16_t)(lo * 2));
        add(events, &count, setup->reset, 0, RASTER_CRTC, CRTC_LO0, lo);
    }
    else if (step == 2)
    {
        uint8_t reg = variant == 0 ? CRTC_LO0 : CRTC_HAJ0;
        uint16_t value = video_register(mode, reg);
        add(events, &count, write, param, RASTER_CRTC, reg, (uint16_t)(variant == 0 ? value * 2 : value + 8));
        add(events, &count, setup->reset, 0, RASTER_CRTC, reg, value);
    }
    else if (step == 3)
    {
        uint16_t zoom = video_register(mode, CRTC_ZOOM);
        add(events, &count, write + variant, 0, RASTER_CRTC, CRTC_ZOOM, zoom);
        add(events, &count, setup->reset, 0, RASTER_CRTC, CRTC_ZOOM, (uint16_t)(zoom | 0x0010));
    }
    else if (step == 4)
    {
        add(events, &count, write, 0, RASTER_CRTC_HIGH, CRTC_FA0, FA_ROW_100 >> 8);
        add(events, &count, setup->reset, 0, RASTER_CRTC, CRTC_FA0, 0);
        add(events, &count, last, 0, RASTER_CRTC_LOW, CRTC_FA0, 0x80);
    }
    else if (step == 5)
    {
        uint16_t vds = video_register(mode, CRTC_VDS0);
        add(events, &count, 0, 0, RASTER_CRTC, CRTC_VDE0, video_register(mode, CRTC_VDE0));
        add(events, &count, write, 0, RASTER_CRTC, CRTC_VDE0, (uint16_t)(vds + 200));
    }
    else
    {
        uint16_t fo = video_register(mode, CRTC_FO0);
        add(events, &count, setup->reset, 0, RASTER_CRTC, CRTC_FO0, fo);
        add(events, &count, moved, 0, RASTER_CRTC, CRTC_FO0, (uint16_t)(fo + FA_ROW_100));
    }

    return count;
}

static void overlay(const setup_t* setup, int step, int variant, int param)
{
    const canvas_t* canvas = &setup->canvas;
    const video_mode_t* mode = setup->mode;
    uint32_t line_ns = setup->raster.line_ns;
    uint32_t khz = video_dot_khz(mode);
    int moved = (setup->reset + param) % setup->frames;
    int row = TEXT_Y;

    canvas_fill(canvas, TEXT_X, TEXT_Y - 2, 384, TEXT_LINES * 10 + 4, CANVAS_BLACK);
    canvas_printf(canvas, TEXT_X, row, 1, CANVAS_CYAN, CANVAS_BLACK, "2.5 STEP %u %s", step + 1, k_steps[step].name);
    row += 10;
    canvas_printf(canvas, TEXT_X, row, 1, CANVAS_WHITE, CANVAS_BLACK, "%s LINE %u NS %u LINES", mode->name, line_ns,
        setup->frames);
    row += 10;
    canvas_printf(canvas, TEXT_X, row, 1, CANVAS_WHITE, CANVAS_BLACK, "VS 0-%u VDS %u VDE %u RESET %u",
        setup->vsync_end, setup->vds, setup->vde, setup->reset);
    row += 10;
    canvas_text(canvas, TEXT_X, row, 1, CANVAS_GRAY, CANVAS_BLACK, "LINES FROM THE VSYNC RISE");
    row += 14;

    if (step == 0 || step == 6)
    {
        const char* what = step == 6 && variant == 1 ? "LO0 X2 AT LINE 200" : step == 6 && variant == 2 ?
            "FO0 +3200H AT X" : "FA0 3200H (ROW 100) AT X";

        canvas_printf(canvas, TEXT_X, row, 1, CANVAS_YELLOW, CANVAS_BLACK, "%s", what);
        row += 10;

        if (step == 0 || variant != 1)
        {
            canvas_printf(canvas, TEXT_X, row, 1, CANVAS_YELLOW, CANVAS_BLACK, "X %u: LINE %u, %u US", param, moved,
                moved * line_ns / 1000u);
            row += 10;
            canvas_printf(canvas, TEXT_X, row, 1, CANVAS_WHITE, CANVAS_BLACK, "X IS %s", moved < setup->vsync_end ?
                "IN VSYNC" : moved < setup->vds ? "BEFORE VDS" : moved < setup->vde ? "IN THE DISPLAY" :
                "AFTER VDE");
            row += 10;
            canvas_text(canvas, TEXT_X, row, 1, CANVAS_GRAY, CANVAS_BLACK, "ROW 100 ON TOP IF FA IS TAKEN");
            row += 10;
            canvas_text(canvas, TEXT_X, row, 1, CANVAS_GRAY, CANVAS_BLACK, "AFTER X; SPLIT AT X: EVERY LINE");
        }
    }
    else if (step == 1)
    {
        canvas_text(canvas, TEXT_X, row, 1, CANVAS_YELLOW, CANVAS_BLACK, "LO0 X2 AT DISPLAY LINE 200");
        row += 10;
        canvas_text(canvas, TEXT_X, row, 1, CANVAS_GRAY, CANVAS_BLACK, "ROWS 200,202..: ADDED");
        row += 10;
        canvas_text(canvas, TEXT_X, row, 1, CANVAS_GRAY, CANVAS_BLACK, "ROWS 400,402..: RE-BASED");
    }
    else if (step == 2)
    {
        uint32_t delay = raster_delay_ns(&setup->raster, (uint32_t)param);

        canvas_printf(canvas, TEXT_X, row, 1, CANVAS_YELLOW, CANVAS_BLACK, "%s AT LINE 200 + D", variant == 0 ?
            "LO0 X2" : "HAJ0 +8");
        row += 10;
        canvas_printf(canvas, TEXT_X, row, 1, CANVAS_YELLOW, CANVAS_BLACK, "D %u: %u NS, %u DOTS", param, delay,
            delay * khz / 1000000u);
        row += 10;
        canvas_printf(canvas, TEXT_X, row, 1, CANVAS_WHITE, CANVAS_BLACK, "HSYNC 0-%u HAJ %u HDS %u",
            video_register(mode, CRTC_HSW1), video_register(mode, CRTC_HAJ0), video_register(mode, CRTC_HDS0));
        row += 10;
        canvas_printf(canvas, TEXT_X, row, 1, CANVAS_WHITE, CANVAS_BLACK, "HDE %u HST %u DOTS",
            video_register(mode, CRTC_HDE0), video_register(mode, CRTC_HST));
        row += 10;
        canvas_text(canvas, TEXT_X, row, 1, CANVAS_GRAY, CANVAS_BLACK, "D COUNTS FROM THE HSYNC RISE");
    }
    else if (step == 3)
    {
        canvas_printf(canvas, TEXT_X, row, 1, CANVAS_YELLOW, CANVAS_BLACK, "ZOOM V2 OFF AT LINE %u", WRITE_LINE +
            variant);
        row += 10;
        canvas_printf(canvas, TEXT_X, row, 1, CANVAS_WHITE, CANVAS_BLACK, "%s LINE OF A ROW PAIR", variant == 0 ?
            "FIRST" : "SECOND");
    }
    else if (step == 4)
    {
        canvas_text(canvas, TEXT_X, row, 1, CANVAS_YELLOW, CANVAS_BLACK, "0442H 80H BEFORE VSYNC,");
        row += 10;
        canvas_text(canvas, TEXT_X, row, 1, CANVAS_YELLOW, CANVAS_BLACK, "0443H 32H AT LINE 200");
        row += 10;
        canvas_text(canvas, TEXT_X, row, 1, CANVAS_GRAY, CANVAS_BLACK, "ROW 1 ON TOP: LOW BYTE ALONE");
        row += 10;
        canvas_text(canvas, TEXT_X, row, 1, CANVAS_GRAY, CANVAS_BLACK, "ROW 0 ON TOP: WAITS FOR HIGH");
    }
    else
    {
        canvas_text(canvas, TEXT_X, row, 1, CANVAS_YELLOW, CANVAS_BLACK, "VDE0 VDS0+200 AT LINE 200");
        row += 10;
        canvas_text(canvas, TEXT_X, row, 1, CANVAS_GRAY, CANVAS_BLACK, "ENDS AT 200: RANGE COMPARE");
        row += 10;
        canvas_text(canvas, TEXT_X, row, 1, CANVAS_GRAY, CANVAS_BLACK, "RUNS ON: EQUALITY MISSED");
    }
}

// Where the writes of the frame just run landed, from the time base; drawn after a change only, as the drawing
// takes a frame
static void written(const setup_t* setup, const raster_event_t* events, int count)
{
    int y = TEXT_Y + (TEXT_LINES - 1) * 10;
    int x = TEXT_X;

    canvas_fill(&setup->canvas, TEXT_X, y, 384, 8, CANVAS_BLACK);
    x += canvas_text(&setup->canvas, x, y, 1, CANVAS_LIME, CANVAS_BLACK, "WROTE");

    for (int i = 0; i < count && i < 3; i++)
    {
        uint32_t time = setup->raster.times[i];
        x += canvas_printf(&setup->canvas, x, y, 1, CANVAS_LIME, CANVAS_BLACK, " L%u %u.%u", events[i].line,
            time * 1000u / setup->raster.line_ns, time * 10000u / setup->raster.line_ns % 10);
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
    const lines_t* result = (const lines_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;
    (void)number;

    shell_line(row++, COLOR_CYAN, "THE PHOTOS ARE THE RESULT");
    shell_line(row++, COLOR_WHITE, "SPIN PASS %u PS", result->pass_ps);

    for (int i = 0; i < MODES; i++)
    {
        if (result->line_ns[i] != 0)
            shell_line(row++, COLOR_WHITE, "%-3s LINE %u NS, %u LINES", k_mode_names[i], result->line_ns[i],
                result->frame_lines[i]);
    }

    row++;
    shell_line(row++, COLOR_GRAY, "STEP MODE VARIANT LAST X OR D");

    for (int step = 0; step < STEPS; step++)
    {
        if (result->ran[step])
            shell_line(row++, COLOR_WHITE, " %u   %-4s %u       %d", step + 1, k_mode_names[result->modes[step]],
                result->variants[step], result->params[step]);
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const lines_t* result = (const lines_t*)data;
    int ran = 0;
    (void)steps;

    for (int step = 0; step < STEPS; step++)
        ran += result->ran[step];

    fmt_format(line, 34, "%u STEPS SHOWN", ran);
    *color = COLOR_WHITE;
}
