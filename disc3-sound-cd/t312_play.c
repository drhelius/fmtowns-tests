#include "tests.h"

// 3.12 CD-DA play timing and end (docs/12, HT-52): how long play takes to start, how exact and how fine sub-Q is over a
// minute, what comes at the end of a range with and without repeat, whether pause and resume keep the position, and
// what the subcode ports 04CCh and 04CDh show while audio plays

#define OFFSETS 4
#define FROM 2
#define REPEATS 8
#define POSITION_US 60000000u
#define POSITION_STEP_US 100000u
#define RAW_SAMPLES 20
#define DRIFTS 12
#define DELTAS 18
#define ENDS 4
#define END_EVENTS 32
#define PAUSE_POINTS 5
#define SUBCODE_CHANGES 16
#define LATENCY_LIMIT 6000000u
#define NO_TIME 0xFFFFFFFFu

typedef struct
{
    uint32_t low;
    uint32_t high;
    uint32_t accept_low;
    uint32_t accept_high;
    uint8_t runs;
    uint8_t failed;
    uint8_t reserved[2];
} latency_t;

typedef struct
{
    uint32_t time;
    uint32_t absolute;
    uint32_t relative;
} position_t;

typedef struct
{
    uint8_t count;
    uint8_t repeat;
    uint8_t track;
    uint8_t reserved;
    cd_packet_t events[END_EVENTS];
} end_t;

typedef struct
{
    latency_t latency[FROM][OFFSETS];
    uint16_t samples;
    uint16_t invalid;
    uint16_t distinct;
    uint16_t reserved;
    position_t raw[RAW_SAMPLES];
    int32_t drift[DRIFTS];
    uint16_t deltas[DELTAS];
    end_t ends[ENDS];
    position_t pause[PAUSE_POINTS];
    cd_keep_t pause_log;
    cd_keep_t resume_log;
    uint8_t subcode_or[2];
    uint8_t subcode_and[2];
    uint32_t subcode_reads;
    uint32_t subcode_nonzero;
    uint8_t subcode_count;
    uint8_t reserved2[3];
    cd_packet_t subcode[SUBCODE_CHANGES];
} play_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void step_latency(play_t* result);
static void step_position(play_t* result);
static void step_ends(play_t* result);
static void step_pause(play_t* result);
static void step_subcode(play_t* result);
static void sample(position_t* position);
static void add_event(end_t* end, uint32_t time, const uint8_t* bytes);

static const test_step_t k_steps[] =
{
    { "START LATENCY", 0 },
    { "SUB-Q FOR 60 S", 0 },
    { "END OF PLAY, REPEAT", 0 },
    { "PAUSE AND RESUME", 0 },
    { "SUBCODE PORTS", 0 },
    { 0, 0 }
};

static const uint32_t k_offsets[OFFSETS] = { 0, 10, 30, 59 };

const test_t g_test_play =
{
    "3.12", "CD-DA PLAY AND END", 0, TEST_RUN_ALL, 1, sizeof(play_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    play_t* result = (play_t*)data;
    cd_log_t log;

    cd_quiet();
    cd_load_toc(&log);

    if ((steps & 0x01) != 0)
        step_latency(result);

    if ((steps & 0x02) != 0)
        step_position(result);

    if ((steps & 0x04) != 0)
        step_ends(result);

    if ((steps & 0x08) != 0)
        step_pause(result);

    if ((steps & 0x10) != 0)
        step_subcode(result);

    cd_quiet();
}

// From the play command to the first sub-Q position at or past the target that moved since the sample before
static void step_latency(play_t* result)
{
    for (int from = 0; from < FROM; from++)
    {
        for (int k = 0; k < OFFSETS; k++)
        {
            latency_t* entry = &result->latency[from][k];
            uint32_t target = cd_track(7) + k_offsets[k] * 75;

            entry->low = NO_TIME;
            entry->accept_low = NO_TIME;

            for (int i = 0; i < REPEATS; i++)
            {
                cd_log_t log;
                cd_subq_t subq;
                uint32_t previous = 0xFFFFFFFFu;
                uint32_t time = NO_TIME;

                shell_step("PLAY %s +%u S %u/%u", from == 0 ? "IDLE" : "AFTER READ", k_offsets[k], i + 1, REPEATS);
                cd_quiet();

                if (from == 0)
                    tb_delay(1000000u);
                else
                    cd_read(&log, CD_READ | CD_STATUS, CD_PAD_LBA, 1);

                cd_play(&log, target, cd_track_end(7) + 1, 0);
                entry->runs++;

                if (log.count != 0)
                {
                    uint32_t accept = log.packets[0].time;
                    entry->accept_low = accept < entry->accept_low ? accept : entry->accept_low;
                    entry->accept_high = accept > entry->accept_high ? accept : entry->accept_high;
                }

                while (tb_now() - log.start < LATENCY_LIMIT)
                {
                    if (!cd_subq(&subq))
                        continue;

                    if (subq.absolute >= target + 150 && previous != 0xFFFFFFFFu && subq.absolute != previous)
                    {
                        time = tb_now() - log.start;
                        break;
                    }

                    previous = subq.absolute;
                }

                if (time == NO_TIME)
                {
                    entry->failed++;
                    continue;
                }

                entry->low = time < entry->low ? time : entry->low;
                entry->high = time > entry->high ? time : entry->high;
            }
        }
    }

    cd_quiet();
}

static void sample(position_t* position)
{
    cd_subq_t subq;

    position->time = tb_now();

    if (cd_subq(&subq))
    {
        position->absolute = subq.absolute;
        position->relative = subq.relative;
    }
    else
    {
        position->absolute = 0xFFFFFFFFu;
        position->relative = 0xFFFFFFFFu;
    }
}

// Every 100 ms for a minute: the position against the time base, every 5 s, and the frames between samples
static void step_position(play_t* result)
{
    cd_log_t log;
    position_t first = { 0, 0, 0 };
    uint32_t previous = 0xFFFFFFFFu;

    cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
    tb_delay(1500000u);
    uint32_t start = tb_now();

    for (uint32_t due = 0; due < POSITION_US; due += POSITION_STEP_US)
    {
        position_t position;

        while (tb_now() - start < due)
            ;

        if (due % 1000000u == 0)
            shell_step("SUB-Q %u S", due / 1000000u);

        sample(&position);
        position.time -= start;

        if (result->samples < RAW_SAMPLES)
            result->raw[result->samples] = position;

        result->samples++;

        if (position.absolute == 0xFFFFFFFFu)
        {
            result->invalid++;
            continue;
        }

        if (first.time == 0 && first.absolute == 0)
            first = position;

        if (position.absolute != previous)
            result->distinct++;

        if (previous != 0xFFFFFFFFu)
        {
            uint32_t delta = position.absolute >= previous ? position.absolute - previous : DELTAS - 1;
            result->deltas[delta < DELTAS - 1 ? delta : DELTAS - 1]++;
        }

        previous = position.absolute;

        if (due % 5000000u == 0 && due / 5000000u < DRIFTS)
        {
            int32_t frames = (int32_t)(position.absolute - first.absolute);
            int32_t expected = (int32_t)((position.time - first.time) * 3u / 40000u);
            result->drift[due / 5000000u] = frames - expected;
        }
    }

    cd_quiet();
}

static void add_event(end_t* end, uint32_t time, const uint8_t* bytes)
{
    if (end->count >= END_EVENTS)
        return;

    end->events[end->count].time = time;
    memcpy(end->events[end->count].bytes, bytes, 4);
    end->count++;
}

// The last 5 s of track 6 (track 7 follows) and of track 7 (the last), without and with repeat; every 200 ms an 80h
// whose answer changed, a packet that came unasked, and every 500 ms the sub-Q position as 06 valid SS FF
static void step_ends(play_t* result)
{
    for (int i = 0; i < ENDS; i++)
    {
        end_t* end = &result->ends[i];
        int track = i < 2 ? 6 : 7;
        uint32_t last = cd_track_end(track) + 1;
        uint8_t previous[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
        cd_log_t log;

        end->track = (uint8_t)track;
        end->repeat = (uint8_t)(i & 1);
        shell_step("END OF TRACK %u%s", track, end->repeat ? ", REPEAT" : "");
        cd_quiet();
        cd_play(&log, last - 375, last, end->repeat);
        uint32_t start = log.start;

        for (int k = 0; k < log.count; k++)
            add_event(end, log.packets[k].time, log.packets[k].bytes);

        for (uint32_t due = 200000u; due < 10000000u; due += 200000u)
        {
            cd_subq_t subq;

            while (tb_now() - start < due)
            {
                if ((cd_master() & CD_MASTER_SRQ) != 0)
                {
                    uint8_t bytes[4];

                    for (int b = 0; b < 4; b++)
                        bytes[b] = in8(CD_COMMAND);

                    add_event(end, tb_now() - start, bytes);
                }
            }

            cd_state(&log);

            if ((log.flags & CD_LOG_STALE) != 0)
                add_event(end, log.start - start, log.stale);

            if (log.count != 0 && memcmp(log.packets[0].bytes, previous, 4) != 0)
            {
                memcpy(previous, log.packets[0].bytes, 4);
                add_event(end, log.start + log.packets[0].time - start, previous);
            }

            if (due % 1000000u == 0 || due % 1000000u == 500000u)
            {
                uint8_t bytes[4] = { 0x06, 0, 0, 0 };
                bytes[1] = (uint8_t)cd_subq(&subq);
                bytes[2] = cd_bcd(subq.absolute / 75 % 60);
                bytes[3] = cd_bcd(subq.absolute % 75);
                add_event(end, tb_now() - start, bytes);
            }
        }
    }

    cd_quiet();
}

static void step_pause(play_t* result)
{
    static const uint8_t k_zero[8];
    cd_log_t log;

    shell_step("PAUSE AND RESUME");
    cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
    uint32_t start = tb_now();
    tb_delay(5000000u);
    sample(&result->pause[0]);
    cd_command(&log, CD_PAUSE | CD_STATUS, k_zero);
    cd_keep(&result->pause_log, &log);
    sample(&result->pause[1]);
    tb_delay(3000000u);
    sample(&result->pause[2]);
    cd_command(&log, CD_RESUME | CD_STATUS, k_zero);
    cd_keep(&result->resume_log, &log);
    sample(&result->pause[3]);
    tb_delay(1000000u);
    sample(&result->pause[4]);

    for (int i = 0; i < PAUSE_POINTS; i++)
        result->pause[i].time -= start;

    cd_quiet();
}

// 04CCh and 04CDh while track 7 plays: every value seen OR'ed and AND'ed, then the changes of 04CCh in a tight loop
static void step_subcode(play_t* result)
{
    cd_log_t log;
    uint8_t last = 0xFF;

    shell_step("SUBCODE PORTS");
    cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
    tb_delay(1500000u);
    result->subcode_and[0] = 0xFF;
    result->subcode_and[1] = 0xFF;
    uint32_t start = tb_now();

    while (tb_now() - start < 2000000u)
    {
        uint8_t status = in8(0x04CC);
        uint8_t value = in8(0x04CD);

        result->subcode_or[0] |= status;
        result->subcode_or[1] |= value;
        result->subcode_and[0] &= status;
        result->subcode_and[1] &= value;
        result->subcode_reads++;
        result->subcode_nonzero += (status | value) != 0;

        if (status != last && result->subcode_count < SUBCODE_CHANGES)
        {
            cd_packet_t* change = &result->subcode[result->subcode_count++];
            change->time = tb_now() - start;
            change->bytes[0] = 0xCC;
            change->bytes[1] = status;
            change->bytes[2] = value;
            change->bytes[3] = 0;
            last = status;
        }
    }

    cd_quiet();
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 7;
}

static void page(const void* data, uint32_t steps, int number)
{
    const play_t* result = (const play_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "PLAY TO A MOVING SUB-Q, MS");
        shell_line(row++, COLOR_GRAY, "FROM   +S  ACCEPT     MOVING      F");

        for (int from = 0; from < FROM; from++)
        {
            for (int k = 0; k < OFFSETS; k++)
            {
                const latency_t* entry = &result->latency[from][k];

                if (entry->runs == 0)
                    continue;

                shell_line(row++, entry->failed != 0 ? COLOR_YELLOW : COLOR_WHITE, "%-5s %3u %4u-%-4u %5u-%-5u %u",
                    from == 0 ? "IDLE" : "READ", k_offsets[k],
                    entry->accept_low == NO_TIME ? 0 : entry->accept_low / 1000, entry->accept_high / 1000,
                    entry->low == NO_TIME ? 0 : entry->low / 1000, entry->high / 1000, entry->failed);
            }
        }

        row++;
        return;
    }

    if (number == 1)
    {
        shell_line(row++, COLOR_CYAN, "SUB-Q EVERY 100 MS FOR 60 S");
        shell_line(row++, COLOR_WHITE, "%u SAMPLES, %u INVALID, %u DISTINCT", result->samples, result->invalid,
            result->distinct);
        shell_line(row++, COLOR_CYAN, "DRIFT IN FRAMES EVERY 5 S");
        shell_line(row++, COLOR_WHITE, "%d %d %d %d %d %d", result->drift[0], result->drift[1], result->drift[2],
            result->drift[3], result->drift[4], result->drift[5]);
        shell_line(row++, COLOR_WHITE, "%d %d %d %d %d %d", result->drift[6], result->drift[7], result->drift[8],
            result->drift[9], result->drift[10], result->drift[11]);
        shell_line(row++, COLOR_CYAN, "FRAMES BETWEEN SAMPLES: COUNT");

        for (int i = 0; i < DELTAS; i += 3)
            shell_line(row++, COLOR_WHITE, "%2u:%-5u %2u:%-5u %2u%s:%-5u", i, result->deltas[i], i + 1,
                result->deltas[i + 1], i + 2, i + 2 == DELTAS - 1 ? "+" : "", result->deltas[i + 2]);

        shell_line(row++, COLOR_GRAY, "75 FRAMES A SECOND: 7-8 A SAMPLE");
        return;
    }

    if (number == 2)
    {
        shell_line(row++, COLOR_CYAN, "FIRST SAMPLES: MS, ABSOLUTE, RELATIVE");

        for (int i = 0; i < RAW_SAMPLES && row <= SHELL_BODY_BOTTOM; i++)
        {
            const position_t* position = &result->raw[i];
            shell_line(row++, COLOR_WHITE, "%5u %6u %6u", position->time / 1000, position->absolute,
                position->relative);
        }

        return;
    }

    if (number <= 4)
    {
        for (int i = (number - 3) * 2; i < (number - 3) * 2 + 2; i++)
        {
            const end_t* end = &result->ends[i];
            shell_line(row++, COLOR_CYAN, "TRACK %u LAST 5 S%s, MS", end->track, end->repeat ? ", REPEAT" : "");
            row = cd_draw_packets(row, end->events, end->count > 18 ? 18 : end->count);
        }

        if (number == 4)
            shell_line(row++, COLOR_GRAY, "06 V SS FF: SUB-Q VALID, POSITION");

        return;
    }

    if (number == 5)
    {
        static const char* const k_points[PAUSE_POINTS] = { "PLAYING", "PAUSED", "3 S LATER", "RESUMED", "1 S LATER" };

        shell_line(row++, COLOR_CYAN, "PAUSE AND RESUME, SUB-Q");

        for (int i = 0; i < PAUSE_POINTS; i++)
            shell_line(row++, COLOR_WHITE, "%-10s %5u MS %6u %6u", k_points[i], result->pause[i].time / 1000,
                result->pause[i].absolute, result->pause[i].relative);

        row = cd_draw_keep(row, "PAUSE", &result->pause_log);
        row = cd_draw_keep(row, "RESUME", &result->resume_log);
        return;
    }

    shell_line(row++, COLOR_CYAN, "04CCH 04CDH WHILE TRACK 7 PLAYS");
    shell_line(row++, COLOR_WHITE, "OR %02X %02X AND %02X %02X", result->subcode_or[0], result->subcode_or[1],
        result->subcode_and[0], result->subcode_and[1]);
    shell_line(row++, COLOR_WHITE, "%u READS, %u NOT ZERO", result->subcode_reads, result->subcode_nonzero);
    row = cd_draw_packets(row, result->subcode, result->subcode_count);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const play_t* result = (const play_t*)data;
    (void)steps;

    fmt_format(line, 34, "START %u MS DRIFT %d SUB %02X", result->latency[0][0].low == NO_TIME ? 0 :
        result->latency[0][0].low / 1000, result->drift[DRIFTS - 1], result->subcode_or[0]);
    *color = COLOR_WHITE;
}
