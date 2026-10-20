#include "tests.h"

// 2.1 Sync timing census (docs/12, HT-8 q1, HT-26): every mode of video.c (the Data Book presets, Cameltry's mode
// with VST 20Bh and 20Ch, the probe's 31 kHz mode, the 15 kHz variants and the shell's screens) against the book's
// timing: frame and VSYNC width from FDA0h bit 0, HSYNC rises per frame and the line period from bit 1, and the HSYNC
// width from the share of polls that read it high

#define MODE_CAPACITY 40
#define PERIODS 8
#define LINE_FRAMES 4
#define LINE_SKIP 20
#define ROWS_PER_PAGE 18
#define SETTLE_US 50000u
#define FRAME_TIMEOUT_US 100000u
#define COUNT_POLLS 1000000u
#define STATUS_TIMEOUT 0x01
#define STATUS_COUNT 0x02
#define NTSC_FIELD_US 16683u
#define NTSC_LINE_NS 63556u
#define NTSC_VSYNC_US 191u

typedef struct
{
    uint16_t frame[PERIODS];
    uint16_t vsync[PERIODS];
    uint16_t hsyncs[PERIODS];
    uint32_t line_time;
    uint16_t lines;
    uint16_t status;
    uint32_t samples;
    uint32_t high_samples;
} sync_t;

typedef struct
{
    uint8_t mode_count;
    uint8_t reserved[3];
    sync_t modes[MODE_CAPACITY];
} sync_census_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void measure(const video_mode_t* mode, sync_t* result);
static int measure_lines(const video_mode_t* mode, sync_t* result);
static uint32_t mean(const uint16_t* values);
static uint32_t line_ns(const sync_t* result);
static uint32_t hsync_ns(const sync_t* result);
static uint32_t poll_ns(const sync_t* result);
static int near(uint32_t value, uint32_t expected, uint32_t tolerance);
static uint32_t half_lines(const sync_t* result);

const test_t g_test_sync =
{
    "2.1", "SYNC TIMING CENSUS", 0, TEST_RUN_ALL, 1, sizeof(sync_census_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    sync_census_t* result = (sync_census_t*)data;
    (void)steps;

    result->mode_count = (uint8_t)(g_video_mode_count < MODE_CAPACITY ? g_video_mode_count : MODE_CAPACITY);

    for (int i = 0; i < result->mode_count; i++)
    {
        shell_step("%s", g_video_modes[i].name);
        measure(&g_video_modes[i], &result->modes[i]);
    }
}

static void measure(const video_mode_t* mode, sync_t* result)
{
    uint32_t rise[PERIODS + 1];
    uint32_t fall;

    video_program(mode);
    tb_delay(SETTLE_US);

    if (!video_wait_rise(VIDEO_VSYNC, FRAME_TIMEOUT_US, &rise[0]))
    {
        result->status |= STATUS_TIMEOUT;
        return;
    }

    for (int i = 0; i < PERIODS; i++)
    {
        if (!video_wait(VIDEO_VSYNC, 0, FRAME_TIMEOUT_US, &fall) ||
            !video_wait(VIDEO_VSYNC, VIDEO_VSYNC, FRAME_TIMEOUT_US, &rise[i + 1]))
        {
            result->status |= STATUS_TIMEOUT;
            return;
        }

        result->vsync[i] = (uint16_t)(fall - rise[i]);
        result->frame[i] = (uint16_t)(rise[i + 1] - rise[i]);
    }

    for (int i = 0; i < PERIODS; i++)
    {
        uint32_t count = video_count_hsyncs(COUNT_POLLS);
        result->hsyncs[i] = (uint16_t)(count < 0xFFFF ? count : 0xFFFF);

        if (count == 0xFFFFFFFFu)
            result->status |= STATUS_COUNT;
    }

    if (!measure_lines(mode, result))
        result->status |= STATUS_COUNT;
}

// Lines inside the display, LINE_SKIP lines after VSYNC ends, from one HSYNC rise to another, four frames
static int measure_lines(const video_mode_t* mode, sync_t* result)
{
    uint32_t frame_lines = ((uint32_t)video_register(mode, CRTC_VST) + 1) / 2;
    uint32_t take = frame_lines > 120 ? frame_lines - 60 : frame_lines / 2;
    uint32_t fall;
    uint32_t high;

    if (take > 400)
        take = 400;

    for (int frame = 0; frame < LINE_FRAMES; frame++)
    {
        if (!video_wait_fall(VIDEO_VSYNC, FRAME_TIMEOUT_US, &fall) ||
            video_time_hsyncs(LINE_SKIP, COUNT_POLLS, &high) == 0)
            return 0;

        uint32_t start = tb_now();
        uint32_t polls = video_time_hsyncs(take, COUNT_POLLS, &high);
        uint32_t end = tb_now();

        if (polls == 0)
            return 0;

        result->samples += polls;
        result->high_samples += high;
        result->line_time += end - start;
        result->lines = (uint16_t)(result->lines + take);
    }

    return 1;
}

static uint32_t mean(const uint16_t* values)
{
    uint32_t sum = 0;

    for (int i = 0; i < PERIODS; i++)
        sum += values[i];

    return (sum + PERIODS / 2) / PERIODS;
}

static uint32_t line_ns(const sync_t* result)
{
    return result->lines != 0 ? result->line_time * 1000u / result->lines : 0;
}

static uint32_t hsync_ns(const sync_t* result)
{
    uint32_t high = result->high_samples;
    uint32_t samples = result->samples;

    while (samples > 400000u)
    {
        high >>= 1;
        samples >>= 1;
    }

    return samples != 0 ? high * 10000u / samples * line_ns(result) / 10000u : 0;
}

static uint32_t poll_ns(const sync_t* result)
{
    return result->samples != 0 ? result->line_time * 1000u / result->samples : 0;
}

static int near(uint32_t value, uint32_t expected, uint32_t tolerance)
{
    return value + tolerance >= expected && value <= expected + tolerance;
}

// The frame in half-lines of the measured line period, rounded
static uint32_t half_lines(const sync_t* result)
{
    uint32_t line = line_ns(result);

    if (line == 0)
        return 0;

    uint32_t sum = 0;

    for (int i = 0; i < PERIODS; i++)
        sum += result->frame[i];

    return (sum * 500u / line + 1) / 2;
}

static int pages(const void* data, uint32_t steps)
{
    const sync_census_t* result = (const sync_census_t*)data;
    int per_set = (result->mode_count + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
    (void)steps;
    return per_set * 2 + 1;
}

static void page(const void* data, uint32_t steps, int number)
{
    const sync_census_t* result = (const sync_census_t*)data;
    int per_set = (result->mode_count + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number < per_set * 2)
    {
        int lines_page = number >= per_set;
        int first = (number % per_set) * ROWS_PER_PAGE;

        shell_line(row++, COLOR_CYAN, "%s", lines_page ? "LINE AND HSYNC IN NS" : "FRAME AND VSYNC IN US, 8 FRAMES");
        shell_line(row++, COLOR_GRAY, "%s", lines_page ? "MODE   LINE   BOOK HSYNC  BOOK POLL" :
            "MODE FRAME  BOOK SP VSYNC BOOK HSYNC");

        for (int i = first; i < first + ROWS_PER_PAGE && i < result->mode_count; i++)
        {
            const video_mode_t* mode = &g_video_modes[i];
            const sync_t* entry = &result->modes[i];
            uint16_t low = 0xFFFF;
            uint16_t high = 0;
            uint16_t shortest = 0xFFFF;
            uint16_t longest = 0;

            if ((entry->status & STATUS_TIMEOUT) != 0)
            {
                shell_line(row++, COLOR_YELLOW, "%-4s NO VSYNC IN 100 MS", mode->name);
                continue;
            }

            if (lines_page)
            {
                shell_line(row++, COLOR_WHITE, "%-4s %6u %6u %5u %5u %4u", mode->name, line_ns(entry),
                    video_line_ns(mode), hsync_ns(entry), video_hsync_ns(mode), poll_ns(entry));
                continue;
            }

            for (int k = 0; k < PERIODS; k++)
            {
                low = entry->hsyncs[k] < low ? entry->hsyncs[k] : low;
                high = entry->hsyncs[k] > high ? entry->hsyncs[k] : high;
                shortest = entry->frame[k] < shortest ? entry->frame[k] : shortest;
                longest = entry->frame[k] > longest ? entry->frame[k] : longest;
            }

            shell_line(row++, COLOR_WHITE, "%-4s %5u %5u %2u %5u %4u %u/%u", mode->name, mean(entry->frame),
                video_frame_us(mode), longest - shortest, mean(entry->vsync), video_vsync_us(mode), low, high);
        }

        return;
    }

    int counted = 0;
    int pulse = 0;
    int blanking = 0;
    int frames = 0;
    int lines = 0;
    int ntsc = 0;
    int low_pulse = 0;

    for (int i = 0; i < result->mode_count; i++)
    {
        const video_mode_t* mode = &g_video_modes[i];
        const sync_t* entry = &result->modes[i];
        uint32_t display = (uint32_t)(uint16_t)(video_register(mode, CRTC_VDE0) - video_register(mode, CRTC_VDS0));
        uint32_t blank = video_frame_us(mode) - display * video_line_ns(mode) / 2000u;
        uint32_t width = mean(entry->vsync);
        uint32_t frame = mean(entry->frame);

        if (entry->status != 0)
            continue;

        counted++;
        pulse += near(width, video_vsync_us(mode), 3 + video_vsync_us(mode) / 20);
        blanking += near(width, blank, 3 + blank / 20);
        frames += near(frame, video_frame_us(mode), 2 + video_frame_us(mode) / 2000);
        lines += near(line_ns(entry), video_line_ns(mode), video_line_ns(mode) / 1000);
        ntsc += near(frame, NTSC_FIELD_US, 5) && near(line_ns(entry), NTSC_LINE_NS, 64) &&
            near(width, NTSC_VSYNC_US, 6);
        low_pulse += hsync_ns(entry) > line_ns(entry) / 2;
    }

    shell_line(row++, COLOR_CYAN, "VERDICTS OVER %u MODES", counted);
    shell_line(row++, COLOR_WHITE, "VSYNC BIT = VST1-VST2 PULSE  %3u", pulse);
    shell_line(row++, COLOR_WHITE, "VSYNC BIT = VERTICAL BLANKING %3u", blanking);
    shell_line(row++, COLOR_WHITE, "FRAME = VST+1 HALF-LINES     %3u", frames);
    shell_line(row++, COLOR_WHITE, "LINE = HST+1 DOTS            %3u", lines);
    shell_line(row++, COLOR_WHITE, "SYNC = FIXED NTSC FIELD      %3u", ntsc);
    shell_line(row++, COLOR_WHITE, "HSYNC BIT LOW IN THE PULSE   %3u", low_pulse);
    row++;

    const video_mode_t* names[2] = { video_find("CAM"), video_find("CAMC") };
    uint32_t counts[2] = { 0, 0 };

    shell_line(row++, COLOR_CYAN, "CAMELTRY'S MODE, 15 KHZ");

    for (int k = 0; k < 2; k++)
    {
        int index = names[k] != 0 ? (int)(names[k] - g_video_modes) : -1;

        if (index < 0 || index >= result->mode_count || result->modes[index].status != 0)
        {
            shell_line(row++, COLOR_GRAY, "VST %03XH NOT MEASURED", k == 0 ? 0x20B : 0x20C);
            continue;
        }

        const sync_t* entry = &result->modes[index];
        counts[k] = half_lines(entry);
        shell_line(row++, COLOR_WHITE, "VST %03XH %5u US %3u HALF-LINES", video_register(names[k], CRTC_VST),
            mean(entry->frame), counts[k]);
        shell_line(row++, COLOR_WHITE, " LINE %u NS, BOOK %u", line_ns(entry), video_line_ns(names[k]));
    }

    if (counts[0] != 0)
    {
        const sync_t* entry = &result->modes[names[0] - g_video_modes];
        uint32_t book = video_line_ns(names[0]);

        if (counts[0] == 525)
            shell_line(row++, COLOR_WHITE, "VST 20BH GIVES 525 HALF-LINES");
        else if (counts[0] == 524 && line_ns(entry) > book + book / 1000)
            shell_line(row++, COLOR_WHITE, "524 HALF-LINES, SLOWER CLOCK");
        else if (counts[0] == 524)
            shell_line(row++, COLOR_WHITE, "VST 20BH GIVES VST+1 = 524");
        else
            shell_line(row++, COLOR_YELLOW, "VST 20BH: NONE OF THE THREE");
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const sync_census_t* result = (const sync_census_t*)data;
    const video_mode_t* cameltry = video_find("CAM");
    int index = cameltry != 0 ? (int)(cameltry - g_video_modes) : -1;
    int timeouts = 0;
    (void)steps;

    for (int i = 0; i < result->mode_count; i++)
        timeouts += result->modes[i].status != 0;

    fmt_format(line, 34, "%u MODES %u TIMEOUT CAM %u HL", result->mode_count, timeouts,
        index >= 0 && index < result->mode_count ? half_lines(&result->modes[index]) : 0);
    *color = timeouts != 0 ? COLOR_YELLOW : COLOR_WHITE;
}
