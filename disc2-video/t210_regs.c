#include <stdarg.h>
#include "tests.h"
#include "canvas.h"
#include "font.h"
#include "input.h"
#include "raster.h"

// 2.10 Sprite registers during a transfer (docs/12, HT-18). 256 entries from index 768 make a 16x16 grid of 16-pixel
// squares, all with OFFS = 1: a column's squares share a fill color and a row's squares show its hex digit, so a photo
// names any square. Times count from the BUSY rise; entry k is due at the measured clear plus k entries. After the
// transfer a step stops the engine and shows the half just drawn (DP1), with its numbers in grid row 0. A repeats the
// step, Run picks its variant (3B: SPEN set at line 100; 4B: a pattern rewritten), Select switches between P18
// (15 kHz) and P20 (31 kHz), B ends it. Step 5 is numeric and runs in Run all

#define STEPS 6
#define FIRST_INDEX 768
#define ENTRIES 256
#define DUE_ENTRY 128
#define PATTERN_BASE 0x80
#define TABLE_BASE 0x100
#define MOVED_FIRST 120
#define MOVED_LAST 136
#define SCAN_CHANGES 24
#define REG6_FRAMES 16
#define SPEN 0x80
#define TEXT_LINE_HEIGHT 8

typedef struct
{
    uint32_t t128[2];
    uint32_t t256[2];
    uint32_t act[4];
    uint32_t busy[4];
    uint8_t page[4];
    uint8_t reg6[4];
    uint32_t spen_rise;
    uint32_t scan[SCAN_CHANGES];
    uint32_t scan_elapsed;
    uint32_t scan_done;
    uint8_t reg6_frames[REG6_FRAMES];
    uint8_t ran[STEPS];
    uint8_t mode[STEPS];
    uint8_t variant[STEPS];
    uint8_t reserved[2];
} regs_t;

typedef struct
{
    const video_mode_t* mode;
    int which;
    uint32_t clear_us;
    uint32_t per_ns;
} setup_t;

uint32_t sprite_scan(uint32_t max_polls, uint32_t* changes, uint32_t capacity);

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void setup_mode(setup_t* setup, regs_t* result, int which);
static void fill_sprites(void);
static void start_engine(int first);
static uint32_t transfer_time(int first);
static uint32_t due_us(const setup_t* setup, int entry);
static void run_action(setup_t* setup, regs_t* result, int step, int variant);
static void action(int step, int variant);
static void undo(int step, int variant);
static void freeze(regs_t* result, int step);
static void spen_at_line(setup_t* setup, regs_t* result);
static void page_and_reg6(regs_t* result);
static int halves(setup_t* setup);
static void status(int half, const char* format, ...);
static void wait_input(int* event);
static const char* busy_verdict(const regs_t* result, int step, int which);

static const uint16_t k_column_colors[16] =
{
    0x03E0, 0x7C00, 0x001F, 0x7FE0, 0x7C1F, 0x03FF, 0x41E0, 0x221F, 0x33F4, 0x7E00, 0x2100, 0x6318, 0x4210, 0x0210,
    0x0208, 0x4010
};

static const test_step_t k_steps[] =
{
    { "OFFSETS AT ENTRY 128", TEST_PHOTO },
    { "FIRST INDEX AT ENTRY 128", TEST_PHOTO },
    { "SPEN AT ENTRY 128, VIDEO", TEST_PHOTO },
    { "ENTRY READS", TEST_PHOTO },
    { "PAGE AND REGISTER 6", 0 },
    { "DP1 AT LINE 100", TEST_PHOTO },
    { 0, 0 }
};

const test_t g_test_regs =
{
    "2.10", "SPRITE REGS WHILE BUSY", "PHOTOS", TEST_RUN_ALL, 1, sizeof(regs_t), k_steps,
    run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    regs_t* result = (regs_t*)data;
    setup_t setup;

    setup_mode(&setup, result, 1);

    for (int step = 0; step < STEPS; step++)
    {
        int variant = 0;
        int event = INPUT_NONE;

        if ((steps & (1u << step)) == 0)
            continue;

        result->ran[step] = 1;

        if (step == 4)
        {
            page_and_reg6(result);
            continue;
        }

        for (;;)
        {
            result->mode[step] = (uint8_t)setup.which;
            result->variant[step] = (uint8_t)variant;

            if (step == 5)
                event = halves(&setup);
            else
            {
                run_action(&setup, result, step, variant);
                wait_input(&event);
            }

            if (event == INPUT_B)
                break;

            if (event == INPUT_SELECT)
                setup_mode(&setup, result, setup.which ^ 1);
            else if (event == INPUT_RUN)
                variant ^= 1;
        }

        start_engine(FIRST_INDEX);
    }

    video_sprite_reg(1, 0x00);
}

static void wait_input(int* event)
{
    while ((*event = input_poll()) == INPUT_NONE)
        ;
}

// The mode with layer 1 in front, as sprite games set it, the grid, and the engine's clear and entry times from 128
// and 256 entries
static void setup_mode(setup_t* setup, regs_t* result, int which)
{
    canvas_t canvas;

    setup->which = which;
    setup->mode = video_find(which ? "P18" : "P20");
    video_sprite_reg(1, 0x00);
    video_program(setup->mode);
    canvas_palettes(setup->mode);
    out8(OUTPUT_ADDRESS, 0x01);
    out8(OUTPUT_DATA, (uint8_t)(setup->mode->output[1] | 0x01));
    canvas_layer(&canvas, which ? 8 : 5, 0);
    canvas_clear(&canvas, CANVAS_DARK);
    fill_sprites();

    uint32_t t128 = transfer_time(FIRST_INDEX + 128);
    uint32_t t256 = transfer_time(FIRST_INDEX);

    result->t128[which] = t128;
    result->t256[which] = t256;
    setup->per_ns = t256 > t128 ? (t256 - t128) * 1000u / 128u : 0;
    setup->clear_us = t128 > setup->per_ns * 128u / 1000u ? t128 - setup->per_ns * 128u / 1000u : 0;
}

// Pattern 80h + r: a white border, the column color inside, r in black; table 100h + c: the colors of column c
static void fill_sprites(void)
{
    volatile uint16_t* ram = (volatile uint16_t*)g_machine.sprite_ram;
    uint32_t time;

    video_sprite_wait(0, &time);

    for (int digit = 0; digit < 16; digit++)
    {
        const uint8_t* glyph = font_glyph((uint8_t)(digit < 10 ? '0' + digit : 'A' + digit - 10));
        volatile uint8_t* pattern = (volatile uint8_t*)g_machine.sprite_ram + (PATTERN_BASE + digit) * 128;

        for (int y = 0; y < 16; y++)
        {
            for (int x = 0; x < 16; x += 2)
            {
                uint8_t pair = 0;

                for (int half = 0; half < 2; half++)
                {
                    int px = x + half;
                    uint8_t value = px == 0 || y == 0 || px == 15 || y == 15 ? 1 : 2;

                    if (px >= 4 && px < 12 && y >= 4 && y < 12 && (glyph[y - 4] & (0x80 >> (px - 4))) != 0)
                        value = 3;

                    pair |= (uint8_t)(value << (half * 4));
                }

                pattern[y * 8 + x / 2] = pair;
            }
        }
    }

    for (int column = 0; column < 16; column++)
    {
        volatile uint16_t* table = ram + (TABLE_BASE + column) * 16;
        table[1] = 0x7FFF;
        table[2] = k_column_colors[column];
        table[3] = 0x0000;
    }

    for (int i = 0; i < ENTRIES; i++)
    {
        volatile uint16_t* entry = ram + (FIRST_INDEX + i) * 4;
        entry[0] = (uint16_t)(16 * (i % 16));
        entry[1] = (uint16_t)(16 * (i / 16));
        entry[2] = (uint16_t)(0x8000 | (PATTERN_BASE + i / 16));
        entry[3] = (uint16_t)(0x8000 | (TABLE_BASE + i % 16));
    }
}

// The first index and SPEN, with no offsets and DP1 = 0
static void start_engine(int first)
{
    for (int reg = 2; reg <= 6; reg++)
        video_sprite_reg((uint8_t)reg, 0x00);

    video_sprite_reg(0, (uint8_t)first);
    video_sprite_reg(1, (uint8_t)(SPEN | ((first >> 8) & 0x03)));
}

// One transfer to take the first index, then the next one from BUSY rise to fall
static uint32_t transfer_time(int first)
{
    uint32_t start;
    uint32_t end;

    start_engine(first);

    if (!video_sprite_wait(0, &start) || !video_sprite_wait(VIDEO_SPRITE_BUSY, &start) ||
        !video_sprite_wait(0, &end) || !video_sprite_wait(VIDEO_SPRITE_BUSY, &start) || !video_sprite_wait(0, &end))
        return 0;

    return end - start;
}

static uint32_t due_us(const setup_t* setup, int entry)
{
    return setup->clear_us + (uint32_t)entry * setup->per_ns / 1000u;
}

// The write when entry 128 is due (half a slot later for the pattern rewrite), then the transfer's end
static void run_action(setup_t* setup, regs_t* result, int step, int variant)
{
    uint32_t start;
    uint32_t end;
    uint32_t due = due_us(setup, DUE_ENTRY) + (step == 3 && variant == 1 ? setup->per_ns / 2000u : 0);
    int index = step < 4 ? step : 3;

    fill_sprites();
    start_engine(FIRST_INDEX);

    if (!video_sprite_wait(0, &start) || !video_sprite_wait(VIDEO_SPRITE_BUSY, &start) ||
        !video_sprite_wait(0, &end) || !video_sprite_wait(VIDEO_SPRITE_BUSY, &start))
        return;

    if (step == 2 && variant == 1)
    {
        video_sprite_wait(0, &end);
        spen_at_line(setup, result);
        return;
    }

    while (tb_now() - start < due)
        ;

    action(step, variant);
    result->act[index] = tb_now() - start;
    video_sprite_wait(0, &end);
    result->busy[index] = end - start;
    freeze(result, index);
    undo(step, variant);
    status(result->page[index], "2.10 %u%s %s", step + 1, variant ? "B" : "", setup->mode->name);
    status(result->page[index] | 0x10, "ACT %u BUSY %u US", result->act[index], result->busy[index]);
}

static void action(int step, int variant)
{
    volatile uint16_t* ram = (volatile uint16_t*)g_machine.sprite_ram;

    if (step == 0)
        video_sprite_reg(2, 4);
    else if (step == 1)
        video_sprite_reg(0, (uint8_t)(FIRST_INDEX + 128));
    else if (step == 2)
        video_sprite_reg(1, (uint8_t)(FIRST_INDEX >> 8));
    else if (variant == 0)
    {
        for (int i = MOVED_FIRST; i <= MOVED_LAST; i++)
            ram[(FIRST_INDEX + i) * 4] = (uint16_t)(ram[(FIRST_INDEX + i) * 4] + 8);
    }
    else
    {
        volatile uint32_t* pattern = (volatile uint32_t*)(g_machine.sprite_ram + (PATTERN_BASE + 8) * 128);

        for (int i = 0; i < 32; i++)
            pattern[i] = 0x33333333u;
    }
}

static void undo(int step, int variant)
{
    if (step == 0)
        video_sprite_reg(2, 0);
    else if (step == 3)
        fill_sprites();

    (void)variant;
}

// The engine stopped and DP1 on the half the transfer drew, which PAGE names until the next VSYNC
static void freeze(regs_t* result, int index)
{
    uint8_t half = in8(VIDEO_SPRITE_STATUS) & VIDEO_SPRITE_PAGE;

    video_sprite_reg(1, (uint8_t)(FIRST_INDEX >> 8));
    video_sprite_reg(6, (uint8_t)(half ? 0x80 : 0x00));
    out8(VIDEO_SPRITE_INDEX, 6);
    result->page[index] = half;
    result->reg6[index] = in8(VIDEO_SPRITE_DATA);
}

// Step 3 variant B: SPEN set at display line 100 with the engine idle, timed to the next BUSY rise
static void spen_at_line(setup_t* setup, regs_t* result)
{
    raster_t raster;
    raster_event_t event;
    uint32_t written;
    uint32_t rise;

    video_sprite_reg(1, (uint8_t)(FIRST_INDEX >> 8));
    raster_setup(&raster, setup->mode);

    event.line = (uint16_t)(((int)video_register(setup->mode, CRTC_VDS0) - (int)video_register(setup->mode,
        CRTC_VST1)) / 2 + 100);
    event.delay = 0;
    event.kind = RASTER_SPRITE;
    event.reg = 1;
    event.value = (uint16_t)(SPEN | (FIRST_INDEX >> 8));
    event.address = 0;

    raster_frame(&raster, &event, 1);
    written = tb_now();
    result->spen_rise = video_sprite_wait(VIDEO_SPRITE_BUSY, &rise) ? rise - written : 0;
    status(0, "2.10 3B SPEN AT LINE 100");
    status(0x10, "BUSY %u US LATER", result->spen_rise);
    video_sprite_reg(6, 0x00);
}

// VSYNC, BUSY and PAGE over 2.3 frames with the full grid, then register 6 at sixteen VSYNC rises
static void page_and_reg6(regs_t* result)
{
    uint32_t changes[8];
    uint32_t time;

    fill_sprites();
    start_engine(FIRST_INDEX);
    video_sprite_wait(VIDEO_SPRITE_BUSY, &time);
    video_sprite_wait(0, &time);

    uint32_t start = tb_now();
    uint32_t done = sprite_scan(2000, changes, 8);
    uint32_t elapsed = tb_now() - start;
    uint32_t polls = elapsed != 0 ? 40000u * done / elapsed : 0;

    for (int i = 0; i < SCAN_CHANGES; i++)
        result->scan[i] = VIDEO_NO_CHANGE;

    video_sprite_wait(VIDEO_SPRITE_BUSY, &time);
    video_sprite_wait(0, &time);
    start = tb_now();
    result->scan_done = sprite_scan(polls, result->scan, SCAN_CHANGES);
    result->scan_elapsed = tb_now() - start;

    for (int k = 0; k < REG6_FRAMES; k++)
    {
        video_vsync_rise(&time);
        out8(VIDEO_SPRITE_INDEX, 6);
        result->reg6_frames[k] = in8(VIDEO_SPRITE_DATA);
    }
}

// Step 6: the halves in two colors with the engine off, DP1 set at display line 100 and cleared after VDE, until a
// key comes
static int halves(setup_t* setup)
{
    canvas_t canvas;
    raster_t raster;
    raster_event_t events[2];
    int event = INPUT_NONE;
    int vst1 = (int)video_register(setup->mode, CRTC_VST1);

    video_sprite_reg(1, (uint8_t)(FIRST_INDEX >> 8));
    canvas_layer(&canvas, setup->which ? 8 : 5, 1);

    for (int half = 0; half < 2; half++)
    {
        canvas_fill(&canvas, 0, half * 256, 256, 256, half ? CANVAS_CYAN : CANVAS_ORANGE);
        canvas_printf(&canvas, 32, half * 256 + 112, 3, CANVAS_BLACK, half ? CANVAS_CYAN : CANVAS_ORANGE,
            "HALF %u", half);
    }

    raster_setup(&raster, setup->mode);
    events[0].line = (uint16_t)(((int)video_register(setup->mode, CRTC_VDS0) - vst1) / 2 + 100);
    events[1].line = (uint16_t)(((int)video_register(setup->mode, CRTC_VDE0) - vst1) / 2 + 2);

    for (int i = 0; i < 2; i++)
    {
        events[i].delay = 0;
        events[i].kind = RASTER_SPRITE;
        events[i].reg = 6;
        events[i].value = (uint16_t)(i == 0 ? 0x80 : 0x00);
        events[i].address = 0;
    }

    while (event == INPUT_NONE)
    {
        raster_frame(&raster, events, 2);
        event = input_poll();
    }

    video_sprite_reg(6, 0x00);
    return event;
}

// A text line in grid row 0 of a half (bit 4 of the argument picks the second line); row 0 is drawn before entry 128
static void status(int half, const char* format, ...)
{
    canvas_t canvas;
    char text[40];
    va_list arguments;

    canvas_layer(&canvas, 8, 1);
    va_start(arguments, format);
    fmt_vformat(text, sizeof(text), format, arguments);
    va_end(arguments);
    canvas_fill(&canvas, 0, (half & 1) * 256 + ((half & 0x10) ? TEXT_LINE_HEIGHT : 0), 256, TEXT_LINE_HEIGHT,
        CANVAS_BLACK);
    canvas_text(&canvas, 0, (half & 1) * 256 + ((half & 0x10) ? TEXT_LINE_HEIGHT : 0), 1, CANVAS_WHITE, CANVAS_BLACK,
        text);
}

static const char* busy_verdict(const regs_t* result, int step, int which)
{
    uint32_t full = result->t256[which];
    uint32_t busy = result->busy[step];

    if (busy == 0 || full == 0)
        return "NOT RUN";

    if (busy + 100 > full && busy < full + 100)
        return step == 1 ? "IND LATCHED" : "SPEN CLEAR: FINISHES";

    if (busy < result->act[step] + 200)
        return step == 1 ? "IND ENDS IT" : "SPEN CLEAR: STOPS";

    return step == 1 ? "IND LIVE" : "SPEN CLEAR: OTHER";
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 2;
}

static void page(const void* data, uint32_t steps, int number)
{
    const regs_t* result = (const regs_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "TRANSFERS, US FROM BUSY RISE");

        for (int which = 0; which < 2; which++)
        {
            if (result->t256[which] != 0)
                shell_line(row++, COLOR_WHITE, "%s 128: %u 256: %u", which ? "P18" : "P20", result->t128[which],
                    result->t256[which]);
        }

        row++;
        shell_line(row++, COLOR_GRAY, "STEP      ACT  BUSY PAGE REG6");

        for (int step = 0; step < 4; step++)
        {
            if (result->busy[step] != 0)
                shell_line(row++, COLOR_WHITE, " %u %6u %6u %4u %02X", step + 1, result->act[step], result->busy[step],
                    result->page[step], result->reg6[step]);
        }

        row++;

        if (result->busy[1] != 0)
            shell_line(row++, COLOR_WHITE, "%s", busy_verdict(result, 1, result->mode[1]));

        if (result->busy[2] != 0)
            shell_line(row++, COLOR_WHITE, "%s", busy_verdict(result, 2, result->mode[2]));

        if (result->spen_rise != 0)
            shell_line(row++, COLOR_WHITE, "SPEN AT LINE 100: BUSY %u US LATER", result->spen_rise);

        return;
    }

    shell_line(row++, COLOR_CYAN, "PAGE AND REGISTER 6");

    if (!result->ran[4])
    {
        shell_line(row++, COLOR_GRAY, "NOT RUN");
        return;
    }

    int32_t origin = -1;

    for (int i = 1; i < SCAN_CHANGES && result->scan[i] != VIDEO_NO_CHANGE; i++)
    {
        if ((result->scan[i] & 0x01) != 0 && (result->scan[i - 1] & 0x01) == 0)
        {
            origin = (int32_t)(result->scan[i] >> 8);
            break;
        }
    }

    shell_line(row++, COLOR_GRAY, "US FROM VSYNC  VSYNC BUSY PAGE");

    for (int i = 0; i < SCAN_CHANGES && i < 12 && result->scan[i] != VIDEO_NO_CHANGE; i++)
    {
        uint32_t state = result->scan[i] & 0xFF;
        int32_t time = video_poll_ns((int32_t)(result->scan[i] >> 8) - (origin >= 0 ? origin : 0),
            result->scan_elapsed, result->scan_done) / 1000;

        shell_line(row++, COLOR_WHITE, "%8d %8u %4u %4u", time, state & 0x01, (state >> 2) & 0x01,
            (state >> 1) & 0x01);
    }

    char line[40];
    int length = fmt_format(line, sizeof(line), "REG 6:");

    for (int k = 0; k < REG6_FRAMES && length < 36; k++)
        length += fmt_format(line + length, (int)sizeof(line) - length, " %02X", result->reg6_frames[k]);

    shell_line(row++, COLOR_WHITE, "%s", line);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const regs_t* result = (const regs_t*)data;
    int toggles = 0;
    (void)steps;

    for (int k = 1; k < REG6_FRAMES; k++)
        toggles += ((result->reg6_frames[k] ^ result->reg6_frames[k - 1]) & 0x10) != 0;

    fmt_format(line, 34, "REG 6 BIT 4 %s", toggles != 0 ? "FOLLOWS THE HALF" : "STAYS");
    *color = COLOR_WHITE;
}
