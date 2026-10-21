#include "tests.h"

// 3.2 SSG-EG inversion (docs/12, HT-13): recorded at 96 kHz. Channel 1 alone with operator 4 at the maximum rates, so
// the level lands on 200h every 48 samples; block 1 F-number 38 (about 2 Hz) keeps the sine almost still, so the
// output follows the envelope. If the new direction inverts the level that reached 200h, shape 0Ah gets a one-sample
// spike at every trough and 0Eh a dip at every peak; if it inverts the level after the restart, a clean triangle

#define CHANNEL 0
#define OPERATOR 0x0C

enum
{
    A_SHAPE,
    A_SLOW,
    A_HIGH
};

typedef struct
{
    uint32_t starts[REC_MAX_STEPS];
} ssg_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void action(int id, int argument, int on);
static void voice(uint8_t shape, uint8_t rate, uint8_t block, uint16_t fnum);

static const rec_step_t k_schedule[] =
{
    { "08H SAWTOOTH CONTROL", 2000, 500, A_SHAPE, 0x08 },
    { "0AH TRIANGLE", 2000, 500, A_SHAPE, 0x0A },
    { "0EH TRIANGLE", 2000, 500, A_SHAPE, 0x0E },
    { "0AH D1R D2R 20", 2000, 500, A_SLOW, 0x0A },
    { "0AH BLOCK 4 400H", 2000, 500, A_HIGH, 0x0A },
    { "0EH BLOCK 4 400H", 2000, 500, A_HIGH, 0x0E },
    { "08H BLOCK 4 400H", 2000, 500, A_HIGH, 0x08 }
};

const test_t g_test_ssg =
{
    "3.2", "SSG-EG INVERSION", "REC", 0, 1, sizeof(ssg_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    ssg_t* result = (ssg_t*)data;
    snd_saved_t saved;
    (void)steps;

    snd_open(&saved);
    shell_step("SCHEDULE");
    rec_play("SSG-EG ON CHANNEL 1", k_schedule, COUNT_OF(k_schedule), action, result->starts);
    snd_close(&saved);
}

static void action(int id, int argument, int on)
{
    if (!on)
    {
        snd_fm_key(CHANNEL, 0);
        return;
    }

    if (id == A_SHAPE)
        voice((uint8_t)argument, 31, 1, 38);
    else if (id == A_SLOW)
        voice((uint8_t)argument, 20, 1, 38);
    else
        voice((uint8_t)argument, 31, 4, 0x400);

    snd_fm_key(CHANNEL, 1);
}

// Operators 1-3 silent; operator 4 TL 0, MUL 1, AR 31, D1R and D2R rate, SL 15, RR 15, the SSG-EG shape
static void voice(uint8_t shape, uint8_t rate, uint8_t block, uint16_t fnum)
{
    snd_fm_tone(CHANNEL, block, fnum, 0x00);
    snd_ym(0, 0x50 + OPERATOR + CHANNEL, 0x1F);
    snd_ym(0, 0x60 + OPERATOR + CHANNEL, rate);
    snd_ym(0, 0x70 + OPERATOR + CHANNEL, rate);
    snd_ym(0, 0x80 + OPERATOR + CHANNEL, 0xFF);
    snd_ym(0, 0x90 + OPERATOR + CHANNEL, shape);
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 1;
}

static void page(const void* data, uint32_t steps, int number)
{
    const ssg_t* result = (const ssg_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;
    (void)number;

    row = rec_draw(row, "STARTS AFTER THE FIRST SYNC TONE", k_schedule, COUNT_OF(k_schedule), result->starts);
    row++;
    shell_line(row++, COLOR_CYAN, "THE RECORDING ANSWERS");
    shell_line(row++, COLOR_WHITE, "SPIKE EVERY 48 SAMPLES IN 0AH AND");
    shell_line(row++, COLOR_WHITE, "A DIP IN 0EH: 200H LEVEL INVERTED");
    shell_line(row++, COLOR_WHITE, "CLEAN TRIANGLES: RESTART INVERTED");
    shell_line(row++, COLOR_WHITE, "D1R D2R 20: CLEAN IN BOTH");
    shell_line(row++, COLOR_WHITE, "BLOCK 4: 1157 HZ HARMONICS ON SPIKES");
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const ssg_t* result = (const ssg_t*)data;
    (void)steps;

    fmt_format(line, 34, "%s", result->starts[0] != 0 ? "SCHEDULE PLAYED" : "NOT PLAYED");
    *color = COLOR_WHITE;
}
