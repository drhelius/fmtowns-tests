#include "tests.h"

// 2.4 CRTC start and timing registers (docs/12, HT-17, HT-31), in the shell's 15 kHz (VST1 = 6) and 31 kHz (VST1 = 0)
// modes: where the first VSYNC and IRQ11 come after START goes 0 to 1, whether the syncs and IRQ11 run while stopped,
// when HST, VST and VST2 written in the display or in VSYNC take effect, and VSYNC with VST1 at or past VST. Last, the
// other two places that show the syncs, FR (CRTC register 1Eh) and CFF86h, edge by edge against FDA0h in a 15, a 31 and
// a 24 kHz mode, and the CRTC's own counters through FR's display bits: where layer 0's window opens and closes after
// VSYNC, the CRTC's own line period, and what stopping the CRTC, a longer HST or a longer VST does to them

#define MODES 2
#define REPEATS 8
#define POINTS 2
#define CASES 3
#define VST1_CASES 4
#define SEQUENCE_FRAMES 4
#define LINE_SPANS 4
#define CHANGES 40
#define WRITE_LINE 100
#define WAIT_POLLS 1000000u
#define LINE_POLLS 100000u
#define COUNT_CHUNK 2000u
#define STOPPED_US 100000u
#define VST1_US 1000000u
#define NO_EDGE 0x7FFFFFFF
#define SOURCE_MODES 3
#define SOURCES 3
#define SOURCE_CHANGES 16
#define SOURCE_CALIBRATION 2000u
#define SOURCE_FRAME_US 22000u
#define SOURCE_STATS_US 50000u
#define STAT_HIGH 0
#define STAT_HSYNC 1
#define STAT_VSYNC 2
#define STAT_POLLS 3
#define STAT_ELAPSED 4
#define STAT_COUNT 5
#define FRAME_TIMEOUT_US 100000u
#define COUNTER_CHANGES 16
#define COUNTER_LINES_US 20000u
#define COUNTER_FRAMES_US 50000u
#define FR_VSYNC 0x04
#define FR_DSPTH0 0x10
#define FR_DSPTV0 0x40

#define STEP_START 0x01
#define STEP_STOPPED 0x02
#define STEP_DISPLAY 0x04
#define STEP_VSYNC 0x08
#define STEP_VST1 0x10
#define STEP_SOURCES 0x20
#define STEP_COUNTERS 0x40

#define CASE_HST 0
#define CASE_VST 1
#define CASE_VST2 2
#define POINT_LINE 0
#define POINT_VSYNC 1

#define COUNT_VSYNC 0
#define COUNT_HSYNC 1
#define COUNT_IRQ 2
#define COUNT_OR 3
#define COUNT_AND 4
#define COUNT_PREVIOUS 5

typedef struct
{
    int32_t first_rise;
    int32_t first_irr;
    int32_t first_fall;
    uint32_t phase;
    uint8_t state;
    uint8_t reserved[3];
} start_t;

typedef struct
{
    uint32_t counts[6];
    uint32_t elapsed;
} count_t;

typedef struct
{
    int32_t rises[SEQUENCE_FRAMES];
    int32_t falls[SEQUENCE_FRAMES];
} sequence_t;

typedef struct
{
    uint32_t vsync[SOURCE_CHANGES];
    uint32_t vsync_elapsed;
    uint32_t vsync_done;
    uint32_t stats[SOURCES][STAT_COUNT];
} sources_t;

typedef struct
{
    uint32_t changes[COUNTER_CHANGES];
    uint32_t elapsed;
    uint32_t done;
    uint32_t lines[4];
} counters_t;

typedef struct
{
    start_t starts[MODES][REPEATS];
    count_t stopped[MODES];
    count_t running[MODES];
    sequence_t sequences[MODES][POINTS][CASES];
    int32_t lines[MODES][POINTS][LINE_SPANS];
    count_t vst1[VST1_CASES];
    sources_t sources[SOURCE_MODES];
    uint8_t fmr;
    uint8_t reserved[3];
    counters_t counters[SOURCE_MODES];
    uint32_t stopped_lines[4];
    uint32_t hst_lines[4];
    counters_t vst_frames;
} crtc_result_t;

uint32_t crtc_sync_scan(uint32_t polls, uint32_t* changes, uint32_t capacity, uint32_t mask, uint32_t fmr);
void crtc_sync_stats(uint32_t source, uint32_t polls, uint32_t* counts);
uint32_t crtc_fr_scan(uint32_t polls, uint32_t* changes, uint32_t capacity, uint32_t mask);
void crtc_line_counts(uint32_t polls, uint32_t* counts);

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void measure_start(const video_mode_t* mode, int k, start_t* start);
static void count_for(count_t* count, uint32_t us);
static void measure_sequence(const video_mode_t* mode, int which, int point, sequence_t* sequence, int32_t* lines);
static void time_lines(int32_t* lines);
static void measure_vst1(const video_mode_t* mode, int which, count_t* count);
static uint16_t changed_value(const video_mode_t* mode, int which);
static int case_register(int which);
static uint32_t line_ns_with(const video_mode_t* mode, uint32_t hst);
static int near(int32_t value, int32_t expected, int32_t tolerance);
static const char* frame_verdict(const sequence_t* sequence, int32_t step);
static const char* width_verdict(const int32_t* widths, int32_t old, int32_t step);
static void draw_writes(const crtc_result_t* result, int point, int row);
static const char* start_verdict(const crtc_result_t* result, int m);
static void measure_sources(const video_mode_t* mode, sources_t* sources, int fmr);
static int edge_times(const uint32_t* changes, int capacity, int bit, uint32_t elapsed, uint32_t done, int32_t* rise,
    int32_t* fall);
static void draw_sources(const crtc_result_t* result, int row);
static void measure_counters(crtc_result_t* result);
static void scan_frames(counters_t* counters, uint32_t us);
static void count_lines(uint32_t* lines, uint32_t us);
static void draw_counters(const crtc_result_t* result, int row);
static void line_period(const uint32_t* lines, char* text, int size);
static int edge_after(const counters_t* counters, int bit, int32_t after, int32_t* rise, int32_t* fall);

static const char* const k_mode_names[MODES] = { "S15", "S31" };
static const char* const k_vst1_names[VST1_CASES] = { "VST1 > VST", "VST1 = VST", "VST1 = VST-1", "COLUMNS" };
static const char* const k_source_modes[SOURCE_MODES] = { "S15", "S31", "P2" };
static const char* const k_source_names[SOURCES] = { "FDA0", "FR", "F86" };

static const test_step_t k_steps[] =
{
    { "START 0 TO 1", 0 },
    { "SYNC WHILE STOPPED", 0 },
    { "WRITES IN THE DISPLAY", 0 },
    { "WRITES IN VSYNC", 0 },
    { "VST1 PAST VST", 0 },
    { "FR AND CFF86H SYNCS", 0 },
    { "CRTC COUNTERS IN FR", 0 },
    { 0, 0 }
};

const test_t g_test_crtc =
{
    "2.4", "CRTC START AND TIMING", 0, TEST_RUN_ALL, 3, sizeof(crtc_result_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    crtc_result_t* result = (crtc_result_t*)data;

    for (int m = 0; m < MODES; m++)
    {
        const video_mode_t* mode = video_find(k_mode_names[m]);
        uint16_t cr0 = video_register(mode, CRTC_CR0);

        video_program(mode);
        tb_delay(50000);

        if ((steps & STEP_START) != 0)
        {
            shell_step("%s START", mode->name);

            for (int k = 0; k < REPEATS; k++)
                measure_start(mode, k, &result->starts[m][k]);
        }

        if ((steps & STEP_STOPPED) != 0)
        {
            shell_step("%s STOPPED", mode->name);
            video_crtc(CRTC_CR0, (uint16_t)(cr0 & ~CRTC_START));
            tb_delay(2000);
            count_for(&result->stopped[m], STOPPED_US);
            video_crtc(CRTC_CR0, cr0);
            tb_delay(50000);
            count_for(&result->running[m], STOPPED_US);
        }

        for (int point = 0; point < POINTS; point++)
        {
            if ((steps & (point == POINT_LINE ? STEP_DISPLAY : STEP_VSYNC)) == 0)
                continue;

            shell_step("%s WRITES %s", mode->name, point == POINT_LINE ? "IN THE DISPLAY" : "IN VSYNC");

            for (int which = 0; which < CASES; which++)
            {
                measure_sequence(mode, which, point, &result->sequences[m][point][which], result->lines[m][point]);
                tb_delay(50000);
            }
        }
    }

    if ((steps & STEP_VST1) != 0)
    {
        const video_mode_t* mode = video_find("S31");

        video_program(mode);
        tb_delay(50000);

        for (int which = 0; which < VST1_CASES; which++)
        {
            shell_step("%s", k_vst1_names[which]);
            measure_vst1(mode, which, &result->vst1[which]);
        }
    }

    if ((steps & STEP_SOURCES) != 0)
    {
        result->fmr = (g_boot.port_0404 & 0x80) == 0;

        for (int i = 0; i < SOURCE_MODES; i++)
        {
            shell_step("%s FR AND CFF86H", k_source_modes[i]);
            measure_sources(video_find(k_source_modes[i]), &result->sources[i], result->fmr);
        }

        video_program(video_find(k_mode_names[0]));
        tb_delay(50000);
    }

    if ((steps & STEP_COUNTERS) != 0)
    {
        measure_counters(result);
        video_program(video_find(k_mode_names[0]));
        tb_delay(50000);
    }
}

// Each mode: FR's VSYNC and DSPTV0 over more than a frame, then DSPTH0 over 20 ms. Then S31 stopped and with
// HST + 64, and S15 with VST + 20 over three frames
static void measure_counters(crtc_result_t* result)
{
    for (int i = 0; i < SOURCE_MODES; i++)
    {
        shell_step("%s CRTC COUNTERS", k_source_modes[i]);
        video_program(video_find(k_source_modes[i]));
        tb_delay(50000);
        scan_frames(&result->counters[i], COUNTER_FRAMES_US);
        count_lines(result->counters[i].lines, COUNTER_LINES_US);
    }

    const video_mode_t* s31 = video_find(k_mode_names[1]);
    uint16_t cr0 = video_register(s31, CRTC_CR0);

    shell_step("S31 COUNTERS STOPPED");
    video_program(s31);
    tb_delay(50000);
    video_crtc(CRTC_CR0, (uint16_t)(cr0 & ~CRTC_START));
    tb_delay(2000);
    count_lines(result->stopped_lines, COUNTER_LINES_US);
    video_crtc(CRTC_CR0, cr0);
    tb_delay(50000);

    shell_step("S31 COUNTERS, HST + 64");
    video_crtc(CRTC_HST, (uint16_t)(video_register(s31, CRTC_HST) + 64));
    tb_delay(50000);
    count_lines(result->hst_lines, COUNTER_LINES_US);
    video_program(s31);

    const video_mode_t* s15 = video_find(k_mode_names[0]);

    shell_step("S15 COUNTERS, VST + 20");
    video_program(s15);
    video_crtc(CRTC_VST, (uint16_t)(video_register(s15, CRTC_VST) + 20));
    tb_delay(50000);
    scan_frames(&result->vst_frames, COUNTER_FRAMES_US);
}

// FR's VSYNC and DSPTV0 edges from the end of one VSYNC
static void scan_frames(counters_t* counters, uint32_t us)
{
    uint32_t calibration[2];
    uint32_t time;

    for (int i = 0; i < COUNTER_CHANGES; i++)
        counters->changes[i] = VIDEO_NO_CHANGE;

    out8(CRTC_ADDRESS, 0x1E);

    uint32_t start = tb_now();
    uint32_t done = crtc_fr_scan(SOURCE_CALIBRATION, calibration, 2, 0);
    uint32_t elapsed = tb_now() - start;

    if (elapsed == 0 || !video_wait_fall(VIDEO_VSYNC, FRAME_TIMEOUT_US, &time))
        return;

    start = tb_now();
    counters->done = crtc_fr_scan(us / elapsed * done, counters->changes, COUNTER_CHANGES, FR_VSYNC | FR_DSPTV0);
    counters->elapsed = tb_now() - start;
}

static void count_lines(uint32_t* lines, uint32_t us)
{
    uint32_t calibration[2] = { 0, 0 };

    out8(CRTC_ADDRESS, 0x1E);
    lines[0] = 0;
    lines[1] = 0;
    lines[2] = 0;
    lines[3] = 0;

    uint32_t start = tb_now();
    crtc_line_counts(SOURCE_CALIBRATION, calibration);
    uint32_t elapsed = tb_now() - start;

    if (elapsed == 0)
        return;

    lines[3] = us * SOURCE_CALIBRATION / elapsed;
    start = tb_now();
    crtc_line_counts(lines[3], lines);
    lines[2] = tb_now() - start;
}

// VSYNC edges of the three over more than a frame from the end of one VSYNC, then each one's HSYNC on its own over
// about three frames: the share of polls that read it set and the time per rise
static void measure_sources(const video_mode_t* mode, sources_t* sources, int fmr)
{
    uint32_t calibration[2];
    uint32_t time;

    video_program(mode);
    tb_delay(50000);
    out8(CRTC_ADDRESS, 0x1E);

    for (int i = 0; i < SOURCE_CHANGES; i++)
        sources->vsync[i] = VIDEO_NO_CHANGE;

    uint32_t start = tb_now();
    uint32_t done = crtc_sync_scan(SOURCE_CALIBRATION, calibration, 2, 0, (uint32_t)fmr);
    uint32_t elapsed = tb_now() - start;

    if (elapsed == 0 || !video_wait_fall(VIDEO_VSYNC, FRAME_TIMEOUT_US, &time))
        return;

    start = tb_now();
    sources->vsync_done = crtc_sync_scan(SOURCE_FRAME_US * done / elapsed, sources->vsync, SOURCE_CHANGES, 0x15,
        (uint32_t)fmr);
    sources->vsync_elapsed = tb_now() - start;

    for (int k = 0; k < SOURCES; k++)
    {
        uint32_t* stats = sources->stats[k];

        if (k == 2 && !fmr)
            continue;

        stats[STAT_HIGH] = 0;
        stats[STAT_HSYNC] = 0;
        stats[STAT_VSYNC] = 0;
        start = tb_now();
        crtc_sync_stats((uint32_t)k, SOURCE_CALIBRATION, stats);
        elapsed = tb_now() - start;

        if (elapsed == 0)
            continue;

        stats[STAT_HIGH] = 0;
        stats[STAT_HSYNC] = 0;
        stats[STAT_VSYNC] = 0;
        stats[STAT_POLLS] = SOURCE_STATS_US / elapsed * SOURCE_CALIBRATION;
        start = tb_now();
        crtc_sync_stats((uint32_t)k, stats[STAT_POLLS], stats);
        stats[STAT_ELAPSED] = tb_now() - start;
    }
}

// Stopped 5 ms into the display for a frame plus 1.3 ms steps, so the restart falls at eight phases
static void measure_start(const video_mode_t* mode, int k, start_t* start)
{
    uint16_t cr0 = video_register(mode, CRTC_CR0);
    uint32_t polls = video_scan_polls(video_frame_us(mode) * 13 / 10, VIDEO_VSYNC | VIDEO_IRQ);
    uint32_t changes[CHANGES];
    uint32_t last;

    start->first_rise = NO_EDGE;
    start->first_irr = NO_EDGE;
    start->first_fall = NO_EDGE;

    if (polls == 0 || !video_vsync_rise(&last) || video_wait_bit(VIDEO_VSYNC, 0, WAIT_POLLS) == 0)
    {
        start->state = 0xFF;
        return;
    }

    tb_delay(5000);
    video_crtc(CRTC_CR0, (uint16_t)(cr0 & ~CRTC_START));
    tb_delay(17000 + (uint32_t)k * 1300);
    out8(VIDEO_VSYNC_CLEAR, 0);

    for (int i = 0; i < CHANGES; i++)
        changes[i] = VIDEO_NO_CHANGE;

    uint32_t begin = tb_now();
    video_crtc(CRTC_CR0, cr0);
    uint32_t done = video_scan(polls, changes, CHANGES, VIDEO_VSYNC | VIDEO_IRQ);
    uint32_t elapsed = tb_now() - begin;

    start->phase = begin - last;
    start->state = (uint8_t)changes[0];

    for (int i = 0; i < CHANGES && changes[i] != VIDEO_NO_CHANGE; i++)
    {
        uint8_t state = (uint8_t)changes[i];
        int32_t time = video_poll_ns((int32_t)(changes[i] >> 8), elapsed, done);

        if ((state & VIDEO_VSYNC) != 0 && start->first_rise == NO_EDGE)
            start->first_rise = time;

        if ((state & VIDEO_VSYNC) == 0 && start->first_rise != NO_EDGE && start->first_fall == NO_EDGE)
            start->first_fall = time;

        if ((state & VIDEO_IRQ) != 0 && start->first_irr == NO_EDGE)
            start->first_irr = time;
    }

    out8(VIDEO_VSYNC_CLEAR, 0);
}

static void count_for(count_t* count, uint32_t us)
{
    count->counts[COUNT_OR] = 0;
    count->counts[COUNT_AND] = 0xFF;
    count->counts[COUNT_PREVIOUS] = in8(VIDEO_STATUS);
    out8(VIDEO_VSYNC_CLEAR, 0);

    uint32_t start = tb_now();

    while (tb_now() - start < us)
        video_count_edges(COUNT_CHUNK, count->counts);

    count->elapsed = tb_now() - start;
}

// Four frames from a VSYNC rise; the register changes at the point in the first frame and goes back at the same
// point in the second. For HST, spans of lines timed right after the change
static void measure_sequence(const video_mode_t* mode, int which, int point, sequence_t* sequence, int32_t* lines)
{
    uint8_t reg = (uint8_t)case_register(which);
    uint16_t original = video_register(mode, reg);
    uint16_t changed = changed_value(mode, which);
    uint32_t polls = video_scan_polls(video_frame_us(mode), VIDEO_HSYNC);
    uint32_t rises[SEQUENCE_FRAMES];
    uint32_t falls[SEQUENCE_FRAMES];
    uint32_t high;
    int frames = 0;

    for (int i = 0; i < SEQUENCE_FRAMES; i++)
    {
        sequence->rises[i] = NO_EDGE;
        sequence->falls[i] = NO_EDGE;
        falls[i] = 0;
    }

    for (; frames < SEQUENCE_FRAMES; frames++)
    {
        int write = frames < 2;
        uint16_t value = frames == 0 ? changed : original;

        if (!video_vsync_rise(&rises[frames]))
            break;

        if (write && point == POINT_VSYNC)
        {
            video_crtc(reg, value);

            if (frames == 0 && which == CASE_HST)
            {
                time_lines(lines);
                continue;
            }
        }

        if (video_wait_bit(VIDEO_VSYNC, 0, WAIT_POLLS) == 0)
            break;

        falls[frames] = tb_now();

        if (write && point == POINT_LINE)
        {
            if (polls == 0 || video_time_hsyncs(WRITE_LINE, polls, &high) == 0)
                break;

            video_crtc(reg, value);

            if (frames == 0 && which == CASE_HST)
                time_lines(lines);
        }
    }

    video_crtc(reg, original);

    for (int i = 0; i < frames; i++)
    {
        sequence->rises[i] = (int32_t)(rises[i] - rises[0]);

        if (falls[i] != 0)
            sequence->falls[i] = (int32_t)(falls[i] - rises[0]);
    }
}

// One HSYNC period, then spans of 8, 8 and 16, timed from rise to rise as 2.1 does
static void time_lines(int32_t* lines)
{
    static const uint32_t k_spans[LINE_SPANS] = { 1, 8, 8, 16 };
    uint32_t high;

    if (video_time_hsyncs(1, LINE_POLLS, &high) == 0)
        return;

    uint32_t previous = tb_now();

    for (int i = 0; i < LINE_SPANS; i++)
    {
        if (video_time_hsyncs(k_spans[i], LINE_POLLS, &high) == 0)
            return;

        uint32_t now = tb_now();
        lines[i] = (int32_t)((now - previous) * 1000u / k_spans[i]);
        previous = now;
    }
}

// The BIOS way: stopped, VST1 and VST2 (and for Columns VST and HST) written, started; then every IRQ11 request and
// bit 0 rise for a second
static void measure_vst1(const video_mode_t* mode, int which, count_t* count)
{
    uint16_t cr0 = video_register(mode, CRTC_CR0);
    uint16_t vst = video_register(mode, CRTC_VST);
    uint16_t vst1 = which == 0 ? (uint16_t)(vst + 16) : which == 1 ? vst : which == 2 ? (uint16_t)(vst - 1) : 0x65FF;
    uint16_t vst2 = which == 3 ? 0xB70F : (uint16_t)(vst1 + 4);

    video_crtc(CRTC_CR0, (uint16_t)(cr0 & ~CRTC_START));
    video_crtc(CRTC_VST1, vst1);
    video_crtc(CRTC_VST2, vst2);

    if (which == 3)
    {
        video_crtc(CRTC_VST, 0x0001);
        video_crtc(CRTC_HST, 0x0000);
    }

    video_crtc(CRTC_CR0, cr0);
    count_for(count, VST1_US);
    video_program(mode);
    tb_delay(50000);
}

static int case_register(int which)
{
    return which == CASE_HST ? CRTC_HST : which == CASE_VST ? CRTC_VST : CRTC_VST2;
}

static uint16_t changed_value(const video_mode_t* mode, int which)
{
    uint16_t step = (uint16_t)(which == CASE_HST ? 64 : which == CASE_VST ? 20 : 4);
    return (uint16_t)(video_register(mode, case_register(which)) + step);
}

static uint32_t line_ns_with(const video_mode_t* mode, uint32_t hst)
{
    return video_line_ns(mode) * (hst + 1) / ((uint32_t)video_register(mode, CRTC_HST) + 1);
}

static int near(int32_t value, int32_t expected, int32_t tolerance)
{
    return value != NO_EDGE && value >= expected - tolerance && value <= expected + tolerance;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 7;
}

static const char* start_verdict(const crtc_result_t* result, int m)
{
    const video_mode_t* mode = video_find(k_mode_names[m]);
    int32_t frame = (int32_t)video_frame_us(mode);
    int32_t top = (int32_t)(video_register(mode, CRTC_VST1) * video_line_ns(mode) / 2);
    const start_t* first = &result->starts[m][0];
    int32_t base = (int32_t)first->phase * 1000 + first->first_rise;
    int at_once = 0;
    int at_top = 0;
    int free_running = 0;

    // Free running: the first rise keeps the schedule it had before the stop, at whatever frame length
    for (int k = 0; k < REPEATS; k++)
    {
        const start_t* start = &result->starts[m][k];
        int32_t when = (int32_t)start->phase * 1000 + start->first_rise - base;

        at_once += near(start->first_rise, 0, 6000);
        at_top += near(start->first_rise, top, 6000);
        free_running += first->first_rise != NO_EDGE && start->first_rise != NO_EDGE && (near(when, 0, 100000) ||
            near(when, frame * 1000, frame * 20) || near(when, -frame * 1000, frame * 20));
    }

    if (at_once == REPEATS && top > 12000)
        return "FRAME AT VSYNC";

    if (at_top == REPEATS)
        return top > 12000 ? "RESTART AT TOP (+VST1)" : "AT ONCE (VST1 = 0)";

    if (free_running == REPEATS)
        return "FREE RUNNING";

    return "NONE OF THE THREE";
}

static void page(const void* data, uint32_t steps, int number)
{
    const crtc_result_t* result = (const crtc_result_t*)data;
    int row = SHELL_BODY_TOP;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "START 0 TO 1, US FROM THE WRITE");

        if ((steps & STEP_START) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        for (int m = 0; m < MODES; m++)
        {
            const video_mode_t* mode = video_find(k_mode_names[m]);

            shell_line(row++, COLOR_GRAY, "%s VST1 %u: %s", mode->name, video_register(mode, CRTC_VST1),
                start_verdict(result, m));

            for (int k = 0; k < REPEATS; k++)
            {
                const start_t* start = &result->starts[m][k];

                shell_line(row++, COLOR_WHITE, "%u %5u %7d %7d %6d %02X", k, start->phase,
                    start->first_rise == NO_EDGE ? -1 : start->first_rise / 1000,
                    start->first_irr == NO_EDGE ? -1 : start->first_irr / 1000,
                    start->first_fall == NO_EDGE ? -1 : start->first_fall / 1000, start->state);
            }
        }

        shell_line(row++, COLOR_GRAY, "K PHASE   VSYNC   IRR11   FALL ST");
        return;
    }

    if (number == 1)
    {
        shell_line(row++, COLOR_CYAN, "SYNC AND IRQ11 IN 100 MS");

        if ((steps & STEP_STOPPED) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        shell_line(row++, COLOR_GRAY, "             VSYNC HSYNC IRQ11 OR AND");

        int stopped_edges = 0;

        for (int m = 0; m < MODES; m++)
        {
            for (int running = 0; running < 2; running++)
            {
                const count_t* count = running ? &result->running[m] : &result->stopped[m];

                shell_line(row++, COLOR_WHITE, "%s %-8s %5u %5u %5u %02X %02X", k_mode_names[m],
                    running ? "RUNNING" : "STOPPED", count->counts[COUNT_VSYNC], count->counts[COUNT_HSYNC],
                    count->counts[COUNT_IRQ], count->counts[COUNT_OR] & 0xFF, count->counts[COUNT_AND] & 0xFF);

                if (!running)
                    stopped_edges += (int)(count->counts[COUNT_VSYNC] + count->counts[COUNT_HSYNC] +
                        count->counts[COUNT_IRQ]);
            }
        }

        row++;
        shell_line(row++, COLOR_WHITE, "STOPPED: %s", stopped_edges == 0 ? "SYNC STOPS" : "SYNC RUNS");
        return;
    }

    if (number == 2 || number == 3)
    {
        int point = number == 2 ? POINT_LINE : POINT_VSYNC;

        shell_line(row++, COLOR_CYAN, "WRITES %s", point == POINT_LINE ? "AT LINE 100" : "AT THE VSYNC RISE");

        if ((steps & (point == POINT_LINE ? STEP_DISPLAY : STEP_VSYNC)) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        draw_writes(result, point, row);
        return;
    }

    if (number == 5)
    {
        shell_line(row++, COLOR_CYAN, "SYNC BITS IN FDA0H, FR, CFF86H, US");

        if ((steps & STEP_SOURCES) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        draw_sources(result, row);
        return;
    }

    if (number == 6)
    {
        shell_line(row++, COLOR_CYAN, "CRTC COUNTERS IN FR, US");

        if ((steps & STEP_COUNTERS) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        draw_counters(result, row);
        return;
    }

    shell_line(row++, COLOR_CYAN, "VST1 AT OR PAST VST, 31 KHZ, 1 S");

    if ((steps & STEP_VST1) == 0)
    {
        shell_line(row++, COLOR_GRAY, "NOT RUN");
        return;
    }

    shell_line(row++, COLOR_GRAY, "CASE         VSYNC IRQ11  EVERY");

    for (int which = 0; which < VST1_CASES; which++)
    {
        const count_t* count = &result->vst1[which];
        uint32_t edges = count->counts[COUNT_VSYNC] > count->counts[COUNT_IRQ] ? count->counts[COUNT_VSYNC] :
            count->counts[COUNT_IRQ];

        if (edges == 0)
            shell_line(row++, COLOR_WHITE, "%-12s %5u %5u NO VSYNC", k_vst1_names[which], count->counts[COUNT_VSYNC],
                count->counts[COUNT_IRQ]);
        else
            shell_line(row++, COLOR_WHITE, "%-12s %5u %5u %6u", k_vst1_names[which], count->counts[COUNT_VSYNC],
                count->counts[COUNT_IRQ], count->elapsed / edges);
    }

    row++;
    shell_line(row++, COLOR_GRAY, "EVERY: US BETWEEN EDGES");
}

// Against the frame after the restore, so a frame length other than the book's still reads as no change
static const char* frame_verdict(const sequence_t* sequence, int32_t step)
{
    int32_t old = sequence->rises[3] - sequence->rises[2];
    int32_t first = sequence->rises[1];
    int32_t second = sequence->rises[2] - sequence->rises[1];

    if (sequence->rises[3] == NO_EDGE)
        return "NO FRAMES";

    if (near(first, old + step, step / 4))
        return "SAME FRAME";

    if (near(first, old, step / 4) && near(second, old + step, step / 4))
        return "NEXT FRAME";

    return near(first, old, step / 4) && near(second, old, step / 4) ? "NO EFFECT" : "?";
}

static const char* width_verdict(const int32_t* widths, int32_t old, int32_t step)
{
    if (near(widths[0], old + step, step / 4))
        return "SAME VSYNC";

    if (near(widths[1], old + step, step / 4))
        return "NEXT VSYNC";

    return near(widths[1], old, step / 4) ? "NO EFFECT" : "?";
}

static void draw_writes(const crtc_result_t* result, int point, int row)
{
    for (int m = 0; m < MODES; m++)
    {
        const video_mode_t* mode = video_find(k_mode_names[m]);
        uint32_t hst = video_register(mode, CRTC_HST);
        const sequence_t* hst_sequence = &result->sequences[m][point][CASE_HST];
        const sequence_t* vst_sequence = &result->sequences[m][point][CASE_VST];
        const sequence_t* vst2_sequence = &result->sequences[m][point][CASE_VST2];
        const int32_t* lines = result->lines[m][point];
        int32_t old_line = (int32_t)video_line_ns(mode);
        int32_t new_line = (int32_t)line_ns_with(mode, hst + 64);
        int32_t step = (new_line - old_line) / 2;
        int32_t old_frame = hst_sequence->rises[3] - hst_sequence->rises[2];
        int32_t next_frame = hst_sequence->rises[2] - hst_sequence->rises[1];
        int32_t scaled = (int32_t)((uint32_t)old_frame * (uint32_t)new_line / (uint32_t)old_line);
        const char* verdict = near(lines[1], new_line, step) ? "NEXT LINE" : near(lines[3], new_line, step) ?
            "LATER LINE" : near(next_frame, scaled, 20) ? "NEXT FRAME" : near(lines[3], lines[1], step) &&
            near(next_frame, old_frame, 20) ? "NO EFFECT" : "?";

        shell_line(row++, COLOR_GRAY, "%s HST +64 %d>%d %s", mode->name, old_line, new_line, verdict);
        shell_line(row++, COLOR_WHITE, " LINES 2-9 %d 18-33 %d", lines[1], lines[3]);
        shell_line(row++, COLOR_WHITE, " FRAMES %d %d %d", hst_sequence->rises[1], next_frame, old_frame);

        int32_t frame_step = (int32_t)(10u * video_line_ns(mode) / 1000u);

        shell_line(row++, COLOR_GRAY, "%s VST +20 (+%d US) %s", mode->name, frame_step,
            frame_verdict(vst_sequence, frame_step));
        shell_line(row++, COLOR_WHITE, " FRAMES %d %d %d", vst_sequence->rises[1], vst_sequence->rises[2] -
            vst_sequence->rises[1], vst_sequence->rises[3] - vst_sequence->rises[2]);

        int32_t width_step = (int32_t)(2u * video_line_ns(mode) / 1000u);
        int32_t widths[SEQUENCE_FRAMES];

        for (int i = 0; i < SEQUENCE_FRAMES; i++)
        {
            widths[i] = vst2_sequence->falls[i] != NO_EDGE && vst2_sequence->rises[i] != NO_EDGE ?
                vst2_sequence->falls[i] - vst2_sequence->rises[i] : -1;
        }

        shell_line(row++, COLOR_GRAY, "%s VST2 +4 (+%d US) %s", mode->name, width_step,
            width_verdict(widths, widths[3], width_step));
        shell_line(row++, COLOR_WHITE, " WIDTHS %d %d %d %d", widths[0], widths[1], widths[2], widths[3]);
        row++;
    }
}

// The first rise of a bit after the scan's first poll, and the fall after it, in ns from the scan's start
static int edge_times(const uint32_t* changes, int capacity, int bit, uint32_t elapsed, uint32_t done, int32_t* rise,
    int32_t* fall)
{
    int previous = -1;

    *rise = NO_EDGE;
    *fall = NO_EDGE;

    for (int i = 0; i < capacity && changes[i] != VIDEO_NO_CHANGE; i++)
    {
        int state = (int)(changes[i] >> bit) & 1;
        int32_t time = video_poll_ns((int32_t)(changes[i] >> 8), elapsed, done);

        if (previous == 0 && state == 1 && *rise == NO_EDGE)
            *rise = time;
        else if (previous == 1 && state == 0 && *rise != NO_EDGE)
        {
            *fall = time;
            return 1;
        }

        previous = state;
    }

    return *rise != NO_EDGE;
}

static void draw_sources(const crtc_result_t* result, int row)
{
    shell_line(row++, COLOR_GRAY, "MODE SRC  VS AT WIDTH HS HIGH  LINE");

    for (int i = 0; i < SOURCE_MODES; i++)
    {
        const sources_t* sources = &result->sources[i];
        int32_t base;
        int32_t base_fall;

        if (!edge_times(sources->vsync, SOURCE_CHANGES, 0, sources->vsync_elapsed, sources->vsync_done, &base,
            &base_fall))
        {
            shell_line(row++, COLOR_YELLOW, "%-4s NO VSYNC IN FDA0H", k_source_modes[i]);
            continue;
        }

        for (int k = 0; k < SOURCES; k++)
        {
            const uint32_t* stats = sources->stats[k];
            uint32_t share = stats[STAT_POLLS] != 0 ? stats[STAT_HIGH] * 1000u / stats[STAT_POLLS] : 0;
            uint32_t line = stats[STAT_HSYNC] != 0 ? stats[STAT_ELAPSED] * 10u / stats[STAT_HSYNC] : 0;
            int32_t rise;
            int32_t fall;

            if (k == 2 && !result->fmr)
            {
                shell_line(row++, COLOR_GRAY, "%-4s %-4s NOT MAPPED", k_source_modes[i], k_source_names[k]);
                continue;
            }

            if (!edge_times(sources->vsync, SOURCE_CHANGES, k * 2, sources->vsync_elapsed, sources->vsync_done, &rise,
                &fall))
            {
                shell_line(row++, COLOR_WHITE, "%-4s %-4s  NONE        %3u.%u%% %3u.%u", k_source_modes[i],
                    k_source_names[k], share / 10, share % 10, line / 10, line % 10);
                continue;
            }

            shell_line(row++, COLOR_WHITE, "%-4s %-4s %5d %5d  %3u.%u%% %3u.%u", k_source_modes[i], k_source_names[k],
                (rise - base) / 1000, fall != NO_EDGE ? (fall - rise) / 1000 : -1, share / 10, share % 10, line / 10,
                line % 10);
        }
    }
}

// The first rise of an FR bit at or after a time, and the fall after it, in ns from the scan's start
static int edge_after(const counters_t* counters, int bit, int32_t after, int32_t* rise, int32_t* fall)
{
    int previous = -1;

    *rise = NO_EDGE;
    *fall = NO_EDGE;

    for (int i = 0; i < COUNTER_CHANGES && counters->changes[i] != VIDEO_NO_CHANGE; i++)
    {
        int state = (int)(counters->changes[i] >> bit) & 1;
        int32_t time = video_poll_ns((int32_t)(counters->changes[i] >> 8), counters->elapsed, counters->done);

        if (previous == 0 && state == 1 && *rise == NO_EDGE && time >= after)
            *rise = time;
        else if (previous == 1 && state == 0 && *rise != NO_EDGE)
        {
            *fall = time;
            return 1;
        }

        previous = state;
    }

    return *rise != NO_EDGE;
}

// The CRTC's line from DSPTH0's rises in microseconds, two decimals, and the share of polls inside the window
static void line_period(const uint32_t* lines, char* text, int size)
{
    uint32_t line = lines[1] != 0 ? lines[2] * 100u / lines[1] : 0;
    uint32_t share = lines[3] != 0 ? lines[0] * 100u / lines[3] : 0;
    fmt_format(text, size, "%3u.%02u %3u%%", line / 100, line % 100, share);
}

static void draw_counters(const crtc_result_t* result, int row)
{
    char text[12];

    shell_line(row++, COLOR_GRAY, "MODE   VDS AT  VDE AT    LINE  H WIN");

    for (int i = 0; i < SOURCE_MODES; i++)
    {
        const counters_t* counters = &result->counters[i];
        int32_t vsync;
        int32_t vsync_end;
        int32_t open;
        int32_t close;

        line_period(counters->lines, text, sizeof(text));

        if (!edge_after(counters, 2, 0, &vsync, &vsync_end) || !edge_after(counters, 6, vsync, &open, &close))
        {
            shell_line(row++, COLOR_YELLOW, "%-4s   NO VSYNC OR WINDOW %s", k_source_modes[i], text);
            continue;
        }

        shell_line(row++, COLOR_WHITE, "%-4s  %6d  %6d  %s", k_source_modes[i], (open - vsync) / 1000,
            close != NO_EDGE ? (close - vsync) / 1000 : -1, text);
    }

    row++;
    shell_line(row++, COLOR_WHITE, "S31 STOPPED: %u RISES, %u%% IN H WIN", result->stopped_lines[1],
        result->stopped_lines[3] != 0 ? result->stopped_lines[0] * 100u / result->stopped_lines[3] : 0);
    line_period(result->hst_lines, text, sizeof(text));
    shell_line(row++, COLOR_WHITE, "S31 HST + 64: LINE %s", text);

    const counters_t* frames = &result->vst_frames;
    int32_t first = NO_EDGE;
    int32_t last = NO_EDGE;
    int rises = 0;
    int previous = -1;

    for (int i = 0; i < COUNTER_CHANGES && frames->changes[i] != VIDEO_NO_CHANGE; i++)
    {
        int state = (int)(frames->changes[i] >> 6) & 1;
        int32_t time = video_poll_ns((int32_t)(frames->changes[i] >> 8), frames->elapsed, frames->done);

        if (previous == 0 && state == 1)
        {
            first = first == NO_EDGE ? time : first;
            last = time;
            rises++;
        }

        previous = state;
    }

    if (rises > 1)
        shell_line(row++, COLOR_WHITE, "S15 VST + 20: WINDOW EVERY %d", (last - first) / (rises - 1) / 1000);
    else
        shell_line(row++, COLOR_YELLOW, "S15 VST + 20: %u WINDOWS", rises);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const crtc_result_t* result = (const crtc_result_t*)data;
    uint32_t stopped = result->stopped[0].counts[COUNT_VSYNC] + result->stopped[0].counts[COUNT_IRQ];

    *color = COLOR_WHITE;

    if ((steps & STEP_START) != 0)
        fmt_format(line, 34, "S15 %s", start_verdict(result, 0));
    else
        fmt_format(line, 34, "STOPPED VSYNC+IRQ %u", stopped);
}
