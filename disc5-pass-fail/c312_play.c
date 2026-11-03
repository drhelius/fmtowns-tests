#include "checks.h"
#include "../disc3-sound-cd/t312_play.c"
#include "cd_checks.h"

#define STEP_ENDS 0x10
#define STEP_PAUSE 0x20

// The index of the first event with those two bytes from that index on, or -1
static int event_find(const end_t* end, int from, uint8_t first, uint8_t second)
{
    for (int i = from; i < end->count && i < END_EVENTS; i++)
    {
        if (end->events[i].bytes[0] == first && end->events[i].bytes[1] == second)
            return i;
    }

    return -1;
}

// A sub-Q sample (06 01 SS FF) behind the one before it: the repeat jumped back
static int loops_back(const end_t* end)
{
    int previous = -1;

    for (int i = 0; i < end->count && i < END_EVENTS; i++)
    {
        const uint8_t* bytes = end->events[i].bytes;

        if (bytes[0] != 0x06 || bytes[1] != 0x01)
            continue;

        int position = (bytes[2] << 8) | bytes[3];

        if (previous >= 0 && position < previous)
            return 1;

        previous = position;
    }

    return 0;
}

// Pause answers with the state, then 12h; a range without repeat ends with 07h and the drive holding its position,
// after which sub-Q is refused; with repeat it jumps back with no 07h
static void evaluate_pause_end(const void* data, uint32_t steps)
{
    const play_t* result = (const play_t*)data;
    const cd_keep_t* pause = &result->pause_log;
    int ends = 1;
    int after = 1;
    int repeats = 1;
    (void)steps;

    for (int i = 0; i < ENDS; i += 2)
    {
        int end = event_find(&result->ends[i], 0, 0x07, 0x01);
        ends &= end >= 0;
        after &= end >= 0 && event_find(&result->ends[i], end, 0x00, 0x01) >= 0 &&
            event_find(&result->ends[i], end, 0x01, 0x01) >= 0;
        repeats &= event_find(&result->ends[i + 1], 0, 0x07, 0x01) < 0 && loops_back(&result->ends[i + 1]);
    }

    check_true("PAUSE: 00 03, 12 01 50-150 MS", keep_is(pause, 0, 0x00, 0x03) && keep_is(pause, 1, 0x12, 0x01) &&
        pause->packets[1].time >= 50000 && pause->packets[1].time <= 150000);
    check_true("RESUME: 00 01 ALONE", result->resume_log.count == 1 && keep_is(&result->resume_log, 0, 0x00, 0x01));
    check_true("SUB-Q PLAYING, NOT PAUSED", result->pause[0].absolute != 0xFFFFFFFFu &&
        result->pause[1].absolute == 0xFFFFFFFFu && result->pause[2].absolute == 0xFFFFFFFFu &&
        result->pause[4].absolute != 0xFFFFFFFFu && result->pause[4].absolute > result->pause[0].absolute);
    check_true("END WITHOUT REPEAT: 07 01", ends);
    check_true("THEN 80H 00 01, SUB-Q 01 01", after);
    check_true("REPEAT: NO 07H, LOOPS BACK", repeats);
}

static const test_t k_test = CHECK_TEST(g_test_play);

const check_group_t g_check_cd_pause_end = { "CD-DA PAUSE AND END (MARTY)", &k_test, STEP_ENDS | STEP_PAUSE, check_marty,
    evaluate_pause_end };
