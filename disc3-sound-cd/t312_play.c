#include "tests.h"

// 3.12 CD-DA play timing and end (docs/12, HT-52): how long play takes to start by distance and by the drive's
// state, near and far after a stop, what a stop leaves the drive in, how exact and how fine sub-Q is over a minute,
// what comes at the end of a range with and without repeat, whether pause and resume keep the position, and what the
// subcode ports 04CCh and 04CDh show while audio plays

#define REPEATS 8
#define STATE_REPEATS 4
#define DISTANCES 5
#define STATES 12
#define FIRST_READ_STATE 7
#define FAR_PLAY_STATE 6
#define NEAR_READ_STATE 11
#define NEAR_LBA 39800u
#define POSITION_US 60000000u
#define POSITION_STEP_US 100000u
#define RAW_SAMPLES 20
#define DRIFTS 12
#define DELTAS 18
#define ENDS 4
#define END_EVENTS 32
#define STOP_EVENTS 16
#define PAUSE_POINTS 5
#define SUBCODE_CHANGES 16
#define LATENCY_LIMIT 8000000u
#define STOP_WATCH_US 12000000u
#define FRAME_US 13333u
#define NO_TIME 0xFFFFFFFFu

typedef struct
{
    uint32_t low;
    uint32_t high;
    uint32_t accept_high;
    uint8_t first[2];
    uint8_t runs;
    uint8_t failed;
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
    uint8_t count;
    uint8_t reserved[3];
    cd_packet_t events[STOP_EVENTS];
} events_t;

typedef struct
{
    latency_t distance[DISTANCES];
    latency_t state[STATES];
    events_t stop;
    events_t restart;
    uint16_t samples;
    uint16_t invalid;
    uint16_t distinct;
    uint16_t reserved;
    position_t raw[RAW_SAMPLES];
    int32_t drift[DRIFTS];
    uint16_t deltas[DELTAS];
    cd_keep_t format0;
    cd_keep_t format1;
    end_t ends[ENDS];
    position_t pause[PAUSE_POINTS];
    uint32_t resume_moving;
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
static void step_distance(play_t* result);
static void step_state(play_t* result);
static void step_stop(play_t* result);
static void step_position(play_t* result);
static void step_ends(play_t* result);
static void step_pause(play_t* result);
static void step_subcode(play_t* result);
static uint32_t timed_play(latency_t* entry, uint32_t target);
static void accepted(latency_t* entry, const cd_log_t* log);
static void add_time(latency_t* entry, uint32_t time);
static void sample(position_t* position);
static void add_event(end_t* end, uint32_t time, const uint8_t* bytes);
static void add_stop_event(events_t* events, uint32_t time, const uint8_t* bytes);
static void add_unexpected(end_t* end, const cd_log_t* log, uint32_t start);
static void draw_latency(int* row, const char* label, const latency_t* entry);

static const test_step_t k_steps[] =
{
    { "START BY DISTANCE", 0 },
    { "START BY STATE", 0 },
    { "STOP, THEN 80H", 0 },
    { "SUB-Q FOR 60 S", 0 },
    { "END OF PLAY, REPEAT", 0 },
    { "PAUSE AND RESUME", 0 },
    { "SUBCODE PORTS", 0 },
    { 0, 0 }
};

static const char* const k_distance_names[DISTANCES] = { "T7 +0", "T7 +59", "30 MIN", "50 MIN", "70 MIN" };
static const char* const k_state_names[STATES] = { "PLAYING", "PAUSED", "STOP +0", "STOP +1", "STOP +3", "STOP +10",
    "70M STOP+3", "RD PLAY", "RD STOP+0", "RD STOP+3", "RD STOP+10", "RD NEAR+3" };
static const uint8_t k_state_stops[STATES] = { 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1 };
static const uint8_t k_state_waits[STATES] = { 0, 0, 0, 1, 3, 10, 3, 0, 0, 3, 10, 3 };

const test_t g_test_play =
{
    "3.12", "CD-DA PLAY AND END", 0, TEST_RUN_ALL, 3, sizeof(play_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    play_t* result = (play_t*)data;
    cd_log_t log;

    cd_quiet();
    cd_load_toc(&log);

    if ((steps & 0x01) != 0)
        step_distance(result);

    if ((steps & 0x02) != 0)
        step_state(result);

    if ((steps & 0x04) != 0)
        step_stop(result);

    if ((steps & 0x08) != 0)
        step_position(result);

    if ((steps & 0x10) != 0)
        step_ends(result);

    if ((steps & 0x20) != 0)
        step_pause(result);

    if ((steps & 0x40) != 0)
        step_subcode(result);

    cd_quiet();
}

// The first packet's time, the worst of them, and the first run's first two bytes
static void accepted(latency_t* entry, const cd_log_t* log)
{
    entry->runs++;

    if (log->count == 0)
        return;

    uint32_t accept = log->packets[0].time;
    entry->accept_high = accept > entry->accept_high ? accept : entry->accept_high;

    if (entry->runs == 1)
        memcpy(entry->first, log->packets[0].bytes, 2);
}

// From the play command to the time audio started: the first sub-Q at or past the target that moved since the sample
// before, less the frames it is past the target. The sample's time is that of its first packet. The drive holds the
// first query until the sound starts, so it waits that long
static uint32_t timed_play(latency_t* entry, uint32_t target)
{
    cd_log_t log;
    uint32_t previous = 0xFFFFFFFFu;
    uint32_t first = CD_HOLD_US;

    cd_play(&log, target, cd_track_end(CD_TRACKS), 0);
    accepted(entry, &log);

    while (tb_now() - log.start < LATENCY_LIMIT)
    {
        cd_subq_t subq;
        cd_log_t query;
        int valid = cd_subq_wait(&subq, &query, 0x01, first);

        first = 500000u;

        if (!valid)
            continue;

        if (subq.absolute >= target + 150 && previous != 0xFFFFFFFFu && subq.absolute != previous)
        {
            uint32_t seen = query.start + (query.count != 0 ? query.packets[0].time : 0) - log.start;
            uint32_t past = (subq.absolute - target - 150) * FRAME_US;
            return seen > past ? seen - past : 0;
        }

        previous = subq.absolute;
    }

    entry->failed++;
    return NO_TIME;
}

static void add_time(latency_t* entry, uint32_t time)
{
    if (time == NO_TIME)
        return;

    entry->low = time < entry->low ? time : entry->low;
    entry->high = time > entry->high ? time : entry->high;
}

// A one-sector read at LBA 1000 leaves the disc spinning and the head on the data track, then a play to each target;
// the next run's read ends the audio, so no stop comes between runs
static void step_distance(play_t* result)
{
    uint32_t targets[DISTANCES] = { cd_track(7), cd_track(7) + 59 * 75, cd_track(10), cd_track(11), cd_track(12) };

    cd_quiet();

    for (int k = 0; k < DISTANCES; k++)
    {
        latency_t* entry = &result->distance[k];
        entry->low = NO_TIME;

        for (int i = 0; i < REPEATS; i++)
        {
            cd_log_t log;

            shell_step("PLAY TO %s %u/%u", k_distance_names[k], i + 1, REPEATS);
            cd_read(&log, CD_READ | CD_STATUS, CD_PAD_LBA, 1);
            add_time(entry, timed_play(entry, targets[k]));
        }
    }

    cd_quiet();
}

// Track 7 plays for 2 s after its sound starts. Plays to track 7 + 10 s: while it plays, after a pause and after a
// stop (84h) and a wait, and to track 12 (70 minutes out) 3 s after a stop; reads of LBA 1000 to their first data
// ready: while it plays and after a stop and a wait, and of LBA 39800 (near the head) 3 s after a stop. Near and far
// after the same stop tell whether the seek runs while the disc spins up
static void step_state(play_t* result)
{
    static const uint8_t k_zero[8];
    uint32_t target = cd_track(7) + 10 * 75;

    for (int k = 0; k < STATES; k++)
    {
        latency_t* entry = &result->state[k];
        entry->low = NO_TIME;

        for (int i = 0; i < STATE_REPEATS; i++)
        {
            cd_log_t log;

            shell_step("%s %u/%u", k_state_names[k], i + 1, STATE_REPEATS);
            cd_quiet();
            cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
            cd_wait_sound();
            tb_delay(2000000u);

            if (k == 1)
            {
                cd_command(&log, CD_PAUSE | CD_STATUS, k_zero);
                tb_delay(1000000u);
            }
            else if (k_state_stops[k])
            {
                cd_stop(&log);
                tb_delay(k_state_waits[k] * 1000000u);
            }

            if (k < FIRST_READ_STATE)
            {
                add_time(entry, timed_play(entry, k == FAR_PLAY_STATE ? cd_track(12) : target));
                continue;
            }

            cd_read(&log, CD_READ | CD_STATUS, k == NEAR_READ_STATE ? NEAR_LBA : CD_PAD_LBA, 1);
            accepted(entry, &log);
            uint32_t ready = cd_time_of(&log, 0x22, 0);

            if (ready == NO_TIME)
                entry->failed++;
            else
                add_time(entry, ready);
        }
    }

    cd_quiet();
}

static void add_stop_event(events_t* events, uint32_t time, const uint8_t* bytes)
{
    if (events->count >= STOP_EVENTS)
        return;

    events->events[events->count].time = time;
    memcpy(events->events[events->count].bytes, bytes, 4);
    events->count++;
}

// 84h 2 s into track 7's sound, then 80h every 200 ms for 12 s: every packet of the stop, every 80h answer that
// changed and every packet that came unasked. Then a play of track 7 from that state, until 80h reports it playing
static void step_stop(play_t* result)
{
    uint8_t previous[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    cd_log_t log;

    shell_step("STOP, THEN 80H");
    cd_quiet();
    cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
    cd_wait_sound();
    tb_delay(2000000u);
    cd_stop(&log);
    uint32_t start = log.start;

    for (int k = 0; k < log.count; k++)
        add_stop_event(&result->stop, log.packets[k].time, log.packets[k].bytes);

    for (uint32_t due = 200000u; due < STOP_WATCH_US; due += 200000u)
    {
        while (tb_now() - start < due)
        {
            if ((cd_master() & CD_MASTER_SRQ) != 0)
            {
                uint8_t bytes[4];

                for (int b = 0; b < 4; b++)
                    bytes[b] = in8(CD_COMMAND);

                add_stop_event(&result->stop, tb_now() - start, bytes);
            }
        }

        cd_state(&log);

        for (int k = 0; k < log.count; k++)
        {
            if (k == 0 && memcmp(log.packets[0].bytes, previous, 4) == 0)
                continue;

            if (k == 0)
                memcpy(previous, log.packets[0].bytes, 4);

            add_stop_event(&result->stop, log.start + log.packets[k].time - start, log.packets[k].bytes);
        }
    }

    shell_step("PLAY AFTER THE STOP");
    cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
    start = log.start;

    for (int k = 0; k < log.count; k++)
        add_stop_event(&result->restart, log.packets[k].time, log.packets[k].bytes);

    memset(previous, 0xFF, sizeof(previous));

    while (tb_now() - start < LATENCY_LIMIT && result->restart.count < STOP_EVENTS)
    {
        cd_state(&log);

        if (log.count == 0 || memcmp(log.packets[0].bytes, previous, 4) == 0)
            continue;

        memcpy(previous, log.packets[0].bytes, 4);
        add_stop_event(&result->restart, log.start + log.packets[0].time - start, previous);

        if (previous[0] == 0x00 && previous[1] == 0x03)
            break;
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

// The raw answers to a sub-Q query with P0 = 00h, as the first builds sent it, and with 01h, as the system ROM does;
// then every 100 ms for a minute: the position against the time base, every 5 s, and the frames between samples
static void step_position(play_t* result)
{
    cd_log_t log;
    cd_subq_t subq;
    position_t first = { 0, 0, 0 };
    uint32_t previous = 0xFFFFFFFFu;

    shell_step("SUB-Q FORMATS");
    cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
    cd_wait_sound();
    tb_delay(1500000u);
    cd_subq_query(&subq, &log, 0x00);
    cd_keep(&result->format0, &log);
    cd_subq_query(&subq, &log, 0x01);
    cd_keep(&result->format1, &log);
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

// A packet that isn't part of the answer asked for: a play done, an error, anything the drive sent on its own
static void add_unexpected(end_t* end, const cd_log_t* log, uint32_t start)
{
    for (int k = 0; k < log->count; k++)
    {
        uint8_t first = log->packets[k].bytes[0];

        if (first != 0x00 && first != 0x18 && first != 0x19 && first != 0x20)
            add_event(end, log->start + log->packets[k].time - start, log->packets[k].bytes);
    }
}

// The last 5 s of track 6 and of track 7 (tracks 7 and 8 follow), without and with repeat; every 200 ms an 80h whose
// answer changed, a packet that came unasked or with another answer, and every 500 ms the position as 06 valid SS FF
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

            add_unexpected(end, &log, start);

            if (due % 1000000u == 0 || due % 1000000u == 500000u)
            {
                uint8_t bytes[4] = { 0x06, 0, 0, 0 };
                bytes[1] = (uint8_t)cd_subq_query(&subq, &log, 0x01);
                bytes[2] = cd_bcd(subq.absolute / 75 % 60);
                bytes[3] = cd_bcd(subq.absolute % 75);
                add_event(end, tb_now() - start, bytes);
                add_unexpected(end, &log, start);
            }
        }
    }

    cd_quiet();
}

// Pause and resume collect their packets until 500 ms pass with none, so a late 12h or 13h is kept; then the time from
// the resume to the first sub-Q past the paused position
static void step_pause(play_t* result)
{
    static const uint8_t k_zero[8];
    cd_log_t log;

    shell_step("PAUSE AND RESUME");
    cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
    uint32_t start = tb_now();
    tb_delay(5000000u);
    sample(&result->pause[0]);
    cd_send(&log, CD_PAUSE | CD_STATUS, k_zero, 0);
    cd_collect(&log, 2000000u, 500000u, 4000000u, 0);
    cd_keep(&result->pause_log, &log);
    sample(&result->pause[1]);
    tb_delay(3000000u);
    sample(&result->pause[2]);
    cd_send(&log, CD_RESUME | CD_STATUS, k_zero, 0);
    uint32_t resumed = log.start;
    cd_collect(&log, 2000000u, 500000u, 4000000u, 0);
    cd_keep(&result->resume_log, &log);
    result->resume_moving = NO_TIME;

    while (tb_now() - resumed < LATENCY_LIMIT && result->pause[2].absolute != 0xFFFFFFFFu)
    {
        cd_subq_t subq;

        if (cd_subq_query(&subq, &log, 0x01) && subq.absolute > result->pause[2].absolute)
        {
            result->resume_moving = log.start + (log.count != 0 ? log.packets[0].time : 0) - resumed;
            break;
        }
    }

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
    cd_wait_sound();
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
    return 10;
}

static void draw_latency(int* row, const char* label, const latency_t* entry)
{
    if (entry->runs == 0)
        return;

    shell_line((*row)++, entry->failed != 0 ? COLOR_YELLOW : COLOR_WHITE, "%-10s %02X%02X %3u %5u-%-5u %u", label,
        entry->first[0], entry->first[1], entry->accept_high / 1000, entry->low == NO_TIME ? 0 : entry->low / 1000,
        entry->high / 1000, entry->failed);
}

static void page(const void* data, uint32_t steps, int number)
{
    const play_t* result = (const play_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "AFTER A READ OF LBA 1000, PLAY TO");
        shell_line(row++, COLOR_GRAY, "TARGET     1ST  ACC START MS    F");

        for (int k = 0; k < DISTANCES; k++)
            draw_latency(&row, k_distance_names[k], &result->distance[k]);

        row++;
        shell_line(row++, COLOR_GRAY, "1ST: FIRST PACKET, ACC: ITS MS");
        shell_line(row++, COLOR_GRAY, "START: FIRST MOVING SUB-Q LESS");
        shell_line(row++, COLOR_GRAY, "ITS FRAMES PAST THE TARGET");
        shell_line(row++, COLOR_GRAY, "30/50/70 MIN: TRACKS 10-12");
        return;
    }

    if (number == 1)
    {
        shell_line(row++, COLOR_CYAN, "AFTER 2 S OF TRACK 7, PLAY T7 +10");
        shell_line(row++, COLOR_GRAY, "FROM       1ST  ACC START MS    F");

        for (int k = 0; k < FIRST_READ_STATE; k++)
            draw_latency(&row, k_state_names[k], &result->state[k]);

        shell_line(row++, COLOR_CYAN, "READ OF LBA 1000 TO 22H");

        for (int k = FIRST_READ_STATE; k < STATES; k++)
            draw_latency(&row, k_state_names[k], &result->state[k]);

        row++;
        shell_line(row++, COLOR_GRAY, "STOP +N: 84H, THEN N S");
        shell_line(row++, COLOR_GRAY, "70M: PLAY TRACK 12 INSTEAD");
        shell_line(row++, COLOR_GRAY, "RD NEAR: LBA 39800 INSTEAD");
        return;
    }

    if (number == 2)
    {
        shell_line(row++, COLOR_CYAN, "84H WHILE PLAYING, 80H 12 S, MS");
        row = cd_draw_packets(row, result->stop.events, result->stop.count);
        shell_line(row++, COLOR_CYAN, "THEN PLAY TRACK 7, MS");
        row = cd_draw_packets(row, result->restart.events, result->restart.count);
        return;
    }

    if (number == 3)
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

    if (number == 4)
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

    if (number == 5)
    {
        shell_line(row++, COLOR_CYAN, "SUB-Q QUERY WHILE TRACK 7 PLAYS");
        row = cd_draw_keep(row, "P0=00H", &result->format0);
        row = cd_draw_keep(row, "P0=01H", &result->format1);
        row++;
        shell_line(row++, COLOR_GRAY, "THE SYSTEM ROM SENDS P0=01H");
        return;
    }

    if (number <= 7)
    {
        for (int i = (number - 6) * 2; i < (number - 6) * 2 + 2; i++)
        {
            const end_t* end = &result->ends[i];
            shell_line(row++, COLOR_CYAN, "TRACK %u LAST 5 S%s, MS", end->track, end->repeat ? ", REPEAT" : "");
            row = cd_draw_packets(row, end->events, end->count > 18 ? 18 : end->count);
        }

        if (number == 7)
            shell_line(row++, COLOR_GRAY, "06 V SS FF: SUB-Q VALID, POSITION");

        return;
    }

    if (number == 8)
    {
        static const char* const k_points[PAUSE_POINTS] = { "PLAYING", "PAUSED", "3 S LATER", "RESUMED", "1 S LATER" };

        shell_line(row++, COLOR_CYAN, "PAUSE AND RESUME, MS, SUB-Q");

        for (int i = 0; i < PAUSE_POINTS; i++)
            shell_line(row++, COLOR_WHITE, "%-9s %5u %6u %6u", k_points[i], result->pause[i].time / 1000,
                result->pause[i].absolute, result->pause[i].relative);

        shell_line(row++, COLOR_WHITE, "MOVING %u MS AFTER RESUME",
            result->resume_moving == NO_TIME ? 0 : result->resume_moving / 1000);
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
    const latency_t* far = &result->distance[2];
    (void)steps;

    fmt_format(line, 34, "30M %u MS DRIFT %d SUB %02X", far->low == NO_TIME ? 0 : far->low / 1000,
        result->drift[DRIFTS - 1], result->subcode_or[0]);
    *color = COLOR_WHITE;
}
