#include "checks.h"
#include "../disc2-video/t29_sprite.c"

#define SCAN_VSYNC 0x01
#define SCAN_PAGE 0x02
#define SCAN_BUSY 0x04

static const char* const k_extra_names[CONTENTION] =
{
    "", "STOSD TO VRAM, EXTRA US", "LODSD FROM SPRITES, EXTRA US", "LODSD FROM VRAM, EXTRA US",
    "STOSD TO SPRITES, EXTRA US", "STOSD TO MAIN RAM, EXTRA US"
};

static int marty_or_20f(void)
{
    return check_marty() || g_machine.model == MODEL_10F_20F;
}

static int32_t scan_us(const sprite_result_t* result, int from, int to)
{
    int32_t polls = (int32_t)(result->scan[to] >> 8) - (int32_t)(result->scan[from] >> 8);
    return video_poll_ns(polls, result->scan_elapsed, result->scan_done) / 1000;
}

// The first change where the bits of mask go from clear to set, or the other way, from index first on
static int scan_edge(const sprite_result_t* result, int first, uint32_t mask, int rise)
{
    for (int i = first > 0 ? first : 1; i < SCAN_CHANGES && result->scan[i] != VIDEO_NO_CHANGE; i++)
    {
        uint32_t now = result->scan[i] & mask;
        uint32_t before = result->scan[i - 1] & mask;

        if (rise ? now != 0 && before == 0 : now == 0 && before != 0)
            return i;
    }

    return -1;
}

static void evaluate_entry(const void* data, uint32_t steps)
{
    const sprite_result_t* result = (const sprite_result_t*)data;
    int32_t per;
    int32_t clear;
    (void)steps;

    if (!fit(result, &per, &clear))
    {
        check_true("TRANSFERS OF 1 TO 1024 ENTRIES", 0);
        return;
    }

    if (check_marty())
    {
        check_signed("NS AN ENTRY, 15 KHZ", per, 68400, 68900);
        check_signed("NS TO CLEAR, 15 KHZ", clear, 55000, 65000);
    }
    else
    {
        check_signed("NS AN ENTRY, 15 KHZ", per, 72200, 72900);
        check_signed("NS TO CLEAR, 15 KHZ", clear, 12000, 28000);
    }
}

static void evaluate_cases(const void* data, uint32_t steps)
{
    const sprite_result_t* result = (const sprite_result_t*)data;
    int32_t low = 0x7FFFFFFF;
    int32_t high = 0;
    (void)steps;

    for (int i = 0; i < PROBE_CASES; i++)
    {
        int32_t per;
        int32_t clear;
        pair_line(result->probe[i], &per, &clear);
        low = per < low ? per : low;
        high = per > high ? per : high;
    }

    check_true("ALL THREE CASES TIMED", low > 0);
    check_range("SPREAD OF NS AN ENTRY", (uint32_t)(high - low), 0, 50);
}

// BUSY and PAGE change together at the start of VSYNC, and BUSY lasts the transfer of 16 entries
static void evaluate_busy(const void* data, uint32_t steps)
{
    const sprite_result_t* result = (const sprite_result_t*)data;
    (void)steps;

    int vsync = scan_edge(result, 1, SCAN_VSYNC, 1);
    int busy = scan_edge(result, 1, SCAN_BUSY, 1);
    int done = busy > 0 ? scan_edge(result, busy, SCAN_BUSY, 0) : -1;
    int again = done > 0 ? scan_edge(result, done, SCAN_BUSY, 1) : -1;

    check_true("VSYNC AND BUSY SEEN", vsync > 0 && busy > 0 && done > 0);

    if (vsync <= 0 || busy <= 0 || done <= 0)
        return;

    check_signed("BUSY RISE TO VSYNC RISE, US", scan_us(result, vsync, busy), -9, check_marty() ? 5 : 13);
    check_true("PAGE FLIPS AS BUSY RISES", ((result->scan[busy] ^ result->scan[busy - 1]) & SCAN_PAGE) != 0);
    check_true("BUSY AGAIN AT THE NEXT VSYNC", again > 0 &&
        ((result->scan[again] ^ result->scan[again - 1]) & SCAN_PAGE) != 0);

    if (check_marty())
        check_signed("VSYNC TO BUSY END, 16 ENTRIES", scan_us(result, vsync, done), 1100, 1200);
    else if (g_machine.model == MODEL_10F_20F)
        check_signed("VSYNC TO BUSY END, 16 ENTRIES", scan_us(result, vsync, done), 1130, 1240);
}

static void evaluate_contention(const void* data, uint32_t steps)
{
    const sprite_result_t* result = (const sprite_result_t*)data;
    int32_t idle = (int32_t)(total(result->contention[0]) / TRANSFERS);
    (void)steps;

    for (int i = 1; i < CONTENTION; i++)
    {
        int32_t extra = (int32_t)(total(result->contention[i]) / TRANSFERS) - idle;

        if (i == 5)
            check_signed(k_extra_names[i], extra, -40, 40);
        else if (i == 2 || i == 3)
            check_signed(k_extra_names[i], extra, 600, 1100);
        else
            check_signed(k_extra_names[i], extra, 2300, 3000);
    }
}

static const test_t k_test = CHECK_TEST(g_test_sprite);

const check_group_t g_check_entry = { "SPRITE ENTRY AND CLEAR TIMES", &k_test, STEP_SWEEP, marty_or_20f,
    evaluate_entry };
const check_group_t g_check_cases = { "SPRITE CONTENT COSTS NOTHING", &k_test, STEP_PROBE, 0, evaluate_cases };
const check_group_t g_check_busy = { "SPRITE BUSY AT VSYNC START", &k_test, STEP_PAGE, 0, evaluate_busy };
const check_group_t g_check_contention = { "CPU SLOWS SPRITES (MARTY)", &k_test, STEP_CONTENTION, check_marty,
    evaluate_contention };
