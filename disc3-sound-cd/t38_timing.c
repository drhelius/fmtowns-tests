#include "tests.h"

// 3.8 CD read timing (docs/12, HT-48): times from the command write to data ready (22h), to read done (06h) and to
// DMA end, the DMA started at each 22h. The sector period of long reads (13.3 ms at 1x), how deep the read-ahead is
// and how fast it fills, seek times by distance in both directions as reads and as 00h seeks, and how fast each
// read form is accepted from idle, from a spinning drive and during CD-DA. Each point keeps the minimum and maximum

#define SPEED_RUNS 2
#define SPEED_SIZES 2
#define INTERVALS 63
#define SINK 260
#define WAITS 7
#define DEPTHS 8
#define REPEATS 8
#define RAW_REPEATS 4
#define DISTANCES 12
#define SEEK_REPEATS 4
#define STATES 3
#define FORMS 3
#define ACCEPT_REPEATS 4
#define AHEAD_LBA 3000u
#define SEEK_LBA CD_PAD_LBA
#define FAST_US 5000u
#define NO_TIME 0xFFFFFFFFu

typedef struct
{
    uint32_t low;
    uint32_t high;
    uint8_t runs;
    uint8_t failed;
    uint8_t reserved[2];
} span_t;

typedef struct
{
    uint32_t first;
    uint32_t last;
    uint32_t done;
    uint32_t shortest;
    uint32_t longest;
    uint16_t sectors;
    uint16_t ready;
} speed_t;

typedef struct
{
    speed_t speed[SPEED_SIZES][SPEED_RUNS];
    uint16_t intervals[INTERVALS];
    uint16_t reserved;
    span_t waits[2][WAITS];
    span_t depths[2][DEPTHS];
    span_t reads[DISTANCES][2];
    span_t seeks[DISTANCES][2];
    span_t accept[STATES][FORMS];
} timing_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void step_speed(timing_t* result);
static void step_ahead(timing_t* result);
static void step_seeks(timing_t* result);
static void step_accept(timing_t* result);
static uint32_t timed_read(uint8_t command, uint32_t lba);
static uint32_t timed_seek(uint32_t lba);
static void span_add(span_t* span, uint32_t time);
static int draw_span(int row, const char* label, const span_t* span);
static uint32_t distance(int index);
static uint32_t mean_interval(const speed_t* speed);
static int depth(const span_t* spans);

static const test_step_t k_steps[] =
{
    { "SPEED, 64 AND 256 SECTORS", 0 },
    { "READ-AHEAD", 0 },
    { "SEEKS", 0 },
    { "COMMAND ACCEPTANCE", 0 },
    { 0, 0 }
};

static const uint32_t k_sizes[SPEED_SIZES] = { 64, 256 };
static const uint32_t k_speed_lba[SPEED_SIZES] = { CD_PAD_LBA, 2000 };
static const uint16_t k_waits[WAITS] = { 0, 7, 14, 27, 54, 100, 500 };
static const uint32_t k_distances[DISTANCES - 1] = { 1, 2, 4, 10, 30, 100, 300, 1000, 3000, 10000, 30000 };
static const uint8_t k_forms[FORMS] = { CD_READ, CD_RAW_READ, CD_SEEK };
static const char* const k_form_names[FORMS] = { "02H", "03H", "00H" };
static const char* const k_state_names[STATES] = { "IDLE", "SPIN", "CD-DA" };

static uint32_t s_sink[SINK];

const test_t g_test_timing =
{
    "3.8", "CD READ TIMING", 0, TEST_RUN_ALL, 1, sizeof(timing_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    timing_t* result = (timing_t*)data;
    cd_log_t log;

    cd_quiet();
    cd_load_toc(&log);

    if ((steps & 0x01) != 0)
        step_speed(result);

    if ((steps & 0x02) != 0)
        step_ahead(result);

    if ((steps & 0x04) != 0)
        step_seeks(result);

    if ((steps & 0x08) != 0)
        step_accept(result);

    cd_sink(0, 0);
    cd_quiet();
}

static void step_speed(timing_t* result)
{
    cd_log_t log;

    cd_sink(s_sink, SINK);

    for (int size = 0; size < SPEED_SIZES; size++)
    {
        for (int i = 0; i < SPEED_RUNS; i++)
        {
            speed_t* speed = &result->speed[size][i];

            shell_step("SPEED %u SECTORS %u/%u", k_sizes[size], i + 1, SPEED_RUNS);
            cd_read(&log, CD_READ | CD_STATUS, k_speed_lba[size], k_sizes[size]);
            speed->sectors = log.sectors;
            speed->ready = log.ready;
            speed->done = (log.flags & CD_LOG_DONE) != 0 ? log.end : NO_TIME;
            speed->shortest = NO_TIME;

            int count = log.ready < SINK ? log.ready : SINK;

            if (count == 0)
                continue;

            speed->first = s_sink[0];
            speed->last = s_sink[count - 1];

            for (int k = 1; k < count; k++)
            {
                uint32_t interval = s_sink[k] - s_sink[k - 1];
                speed->shortest = interval < speed->shortest ? interval : speed->shortest;
                speed->longest = interval > speed->longest ? interval : speed->longest;

                if (size == 0 && i == 0 && k - 1 < INTERVALS)
                    result->intervals[k - 1] = (uint16_t)(interval < 0xFFFF ? interval : 0xFFFF);
            }
        }
    }

    cd_sink(0, 0);
}

// A read, a wait, the next sector; then a read, 200 ms, and the sector k ahead
static void step_ahead(timing_t* result)
{
    for (int form = 0; form < 2; form++)
    {
        uint8_t command = (uint8_t)((form == 0 ? CD_READ : CD_RAW_READ) | CD_STATUS);
        int repeats = form == 0 ? REPEATS : RAW_REPEATS;

        for (int w = 0; w < WAITS; w++)
        {
            for (int i = 0; i < repeats; i++)
            {
                shell_step("%s WAIT %u MS %u/%u", form == 0 ? "READ" : "RAW", k_waits[w], i + 1, repeats);
                timed_read(command, AHEAD_LBA);
                tb_delay((uint32_t)k_waits[w] * 1000u);
                span_add(&result->waits[form][w], timed_read(command, AHEAD_LBA + 1));
            }
        }

        for (int k = 0; k < DEPTHS; k++)
        {
            for (int i = 0; i < repeats; i++)
            {
                shell_step("%s AHEAD +%u %u/%u", form == 0 ? "READ" : "RAW", k + 1, i + 1, repeats);
                timed_read(command, AHEAD_LBA);
                tb_delay(200000u);
                span_add(&result->depths[form][k], timed_read(command, AHEAD_LBA + 1 + (uint32_t)k));
            }
        }
    }
}

static void step_seeks(timing_t* result)
{
    for (int d = 0; d < DISTANCES; d++)
    {
        uint32_t target = SEEK_LBA + distance(d);

        for (int i = 0; i < REPEATS; i++)
        {
            shell_step("SEEK READ +%u %u/%u", distance(d), i + 1, REPEATS);
            timed_read(CD_READ | CD_STATUS, SEEK_LBA);
            span_add(&result->reads[d][0], timed_read(CD_READ | CD_STATUS, target));
            span_add(&result->reads[d][1], timed_read(CD_READ | CD_STATUS, SEEK_LBA));
        }

        for (int i = 0; i < SEEK_REPEATS; i++)
        {
            shell_step("SEEK 00H +%u %u/%u", distance(d), i + 1, SEEK_REPEATS);
            timed_seek(SEEK_LBA);
            span_add(&result->seeks[d][0], timed_seek(target));
            span_add(&result->seeks[d][1], timed_seek(SEEK_LBA));
        }
    }
}

// The time to the first packet of each form, from 3 s idle, right after a read, and while track 7 plays
static void step_accept(timing_t* result)
{
    for (int state = 0; state < STATES; state++)
    {
        for (int form = 0; form < FORMS; form++)
        {
            for (int i = 0; i < ACCEPT_REPEATS; i++)
            {
                uint8_t parameters[8];
                cd_log_t log;

                shell_step("ACCEPT %s %s %u/%u", k_form_names[form], k_state_names[state], i + 1, ACCEPT_REPEATS);

                if (state == 0)
                    tb_delay(3000000u);
                else if (state == 1)
                    timed_read(CD_READ | CD_STATUS, CD_PAD_LBA);
                else
                {
                    cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
                    tb_delay(1000000u);
                }

                cd_range(parameters, CD_PAD_LBA + 500, CD_PAD_LBA + 500);
                cd_arm(k_forms[form], 1);
                cd_send(&log, (uint8_t)(k_forms[form] | CD_STATUS), parameters, 0);
                log.lba = CD_PAD_LBA + 500;
                cd_collect(&log, 4000000u, 2000000u, 6000000u,
                    k_forms[form] == CD_SEEK ? CD_UNTIL_SEEK : CD_DMA | CD_UNTIL_END);
                out8(0x00AF, 0x0F);
                span_add(&result->accept[state][form], log.count != 0 ? log.packets[0].time : NO_TIME);

                if (state == 2)
                    cd_quiet();
            }
        }
    }
}

static uint32_t timed_read(uint8_t command, uint32_t lba)
{
    cd_log_t log;

    cd_read(&log, command, lba, 1);

    if (!cd_read_ok(&log, 1))
        return NO_TIME;

    return cd_time_of(&log, 0x22, 0);
}

static uint32_t timed_seek(uint32_t lba)
{
    uint8_t parameters[8];
    cd_log_t log;

    cd_range(parameters, lba, lba);
    cd_send(&log, CD_SEEK | CD_STATUS, parameters, 0);
    cd_collect(&log, 4000000u, 3000000u, 6000000u, CD_UNTIL_SEEK);
    return (log.flags & CD_LOG_ERROR) != 0 ? NO_TIME : cd_time_of(&log, 0x04, 0);
}

static void span_add(span_t* span, uint32_t time)
{
    if (span->runs == 0)
        span->low = NO_TIME;

    span->runs++;

    if (time == NO_TIME)
    {
        span->failed++;
        return;
    }

    span->low = time < span->low ? time : span->low;
    span->high = time > span->high ? time : span->high;
}

static uint32_t distance(int index)
{
    return index < DISTANCES - 1 ? k_distances[index] : CD_DATA_SECTORS - 1 - SEEK_LBA;
}

static uint32_t mean_interval(const speed_t* speed)
{
    return speed->ready > 1 ? (speed->last - speed->first) / (uint32_t)(speed->ready - 1) : 0;
}

// The sectors ahead that came fast after 200 ms
static int depth(const span_t* spans)
{
    int count = 0;

    while (count < DEPTHS && spans[count].runs != 0 && spans[count].low != NO_TIME && spans[count].low < FAST_US)
        count++;

    return count;
}

// Minimum-maximum in ms with one decimal, and the failed runs
static int draw_span(int row, const char* label, const span_t* span)
{
    if (span->runs == 0)
    {
        shell_line(row++, COLOR_GRAY, "%-9s NOT RUN", label);
        return row;
    }

    if (span->low == NO_TIME)
    {
        shell_line(row++, COLOR_YELLOW, "%-9s ALL %u FAILED", label, span->runs);
        return row;
    }

    char text[24];
    fmt_format(text, sizeof(text), "%u.%u-%u.%u", span->low / 1000, span->low / 100 % 10, span->high / 1000,
        span->high / 100 % 10);
    shell_line(row++, span->failed != 0 ? COLOR_YELLOW : COLOR_WHITE, "%-9s %-17s F%u", label, text, span->failed);
    return row;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 6;
}

static void page(const void* data, uint32_t steps, int number)
{
    const timing_t* result = (const timing_t*)data;
    int row = SHELL_BODY_TOP;
    char label[16];
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "SPEED, MS FROM THE COMMAND");
        shell_line(row++, COLOR_GRAY, "SIZE FIRST22   DONE  MEAN MIN-MAX US");

        for (int size = 0; size < SPEED_SIZES; size++)
        {
            for (int i = 0; i < SPEED_RUNS; i++)
            {
                const speed_t* speed = &result->speed[size][i];

                if (speed->ready == 0)
                {
                    shell_line(row++, COLOR_YELLOW, "%4u NO DATA READY", k_sizes[size]);
                    continue;
                }

                shell_line(row++, speed->sectors == k_sizes[size] ? COLOR_WHITE : COLOR_YELLOW,
                    "%4u %6u %6u %5u %5u-%-5u", k_sizes[size], speed->first / 1000,
                    speed->done == NO_TIME ? 0 : speed->done / 1000, mean_interval(speed), speed->shortest,
                    speed->longest);
            }
        }

        uint32_t mean = mean_interval(&result->speed[1][0]);
        shell_line(row++, COLOR_GREEN, "SECTOR PERIOD %u US: %s", mean, mean > 10000 ? "1X" :
            mean > 5000 ? "2X" : mean != 0 ? "FASTER" : "NONE");
        row++;
        shell_line(row++, COLOR_CYAN, "64-SECTOR RUN, INTERVALS IN US");

        for (int i = 0; i < 48 && row <= SHELL_BODY_BOTTOM; i += 4)
            shell_line(row++, COLOR_WHITE, "%2u %6u %6u %6u %6u", i + 1, result->intervals[i],
                result->intervals[i + 1], result->intervals[i + 2], result->intervals[i + 3]);

        return;
    }

    if (number == 1 || number == 2)
    {
        int form = number - 1;
        shell_line(row++, COLOR_CYAN, "%s: READ, WAIT, NEXT SECTOR", form == 0 ? "02H" : "03H RAW");
        shell_line(row++, COLOR_GRAY, "WAIT MS   22H MIN-MAX MS   FAILED");

        for (int w = 0; w < WAITS; w++)
        {
            fmt_format(label, sizeof(label), "%u", k_waits[w]);
            row = draw_span(row, label, &result->waits[form][w]);
        }

        row++;
        shell_line(row++, COLOR_CYAN, "READ, 200 MS, SECTOR K AHEAD");

        for (int k = 0; k < DEPTHS; k++)
        {
            fmt_format(label, sizeof(label), "+%u", k + 1);
            row = draw_span(row, label, &result->depths[form][k]);
        }

        shell_line(row++, COLOR_GREEN, "DEPTH %u SECTORS (8 KB: %u)", depth(result->depths[form]), form == 0 ? 4 : 3);
        return;
    }

    if (number == 3 || number == 4)
    {
        int seek = number - 3;
        shell_line(row++, COLOR_CYAN, "%s FROM LBA %u, MS", seek ? "00H SEEKS TO 04H" : "READS TO 22H",
            SEEK_LBA);
        shell_line(row++, COLOR_GRAY, "SECTORS FORWARD MIN-MAX  BACK MIN-MAX");

        for (int d = 0; d < DISTANCES; d++)
        {
            const span_t* spans = seek ? result->seeks[d] : result->reads[d];
            char text[2][16];

            for (int k = 0; k < 2; k++)
            {
                if (spans[k].runs == 0 || spans[k].low == NO_TIME)
                    fmt_format(text[k], sizeof(text[k]), "%s", spans[k].runs == 0 ? "-" : "FAILED");
                else
                    fmt_format(text[k], sizeof(text[k]), "%u.%u-%u.%u", spans[k].low / 1000, spans[k].low / 100 % 10,
                        spans[k].high / 1000, spans[k].high / 100 % 10);
            }

            shell_line(row++, spans[0].failed + spans[1].failed != 0 ? COLOR_YELLOW : COLOR_WHITE, "+%-6u %-16s %s",
                distance(d), text[0], text[1]);
        }

        shell_line(row++, COLOR_GRAY, "YELLOW: SOME RUNS FAILED (QR HAS N)");
        return;
    }

    shell_line(row++, COLOR_CYAN, "TIME TO THE FIRST PACKET, MS");

    for (int state = 0; state < STATES; state++)
    {
        for (int form = 0; form < FORMS; form++)
        {
            fmt_format(label, sizeof(label), "%s %s", k_form_names[form], k_state_names[state]);
            row = draw_span(row, label, &result->accept[state][form]);
        }
    }

}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const timing_t* result = (const timing_t*)data;
    (void)steps;

    fmt_format(line, 34, "PERIOD %u US AHEAD %u/%u", mean_interval(&result->speed[1][0]), depth(result->depths[0]),
        depth(result->depths[1]));
    *color = COLOR_WHITE;
}
