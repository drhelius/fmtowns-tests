#include "checks.h"
#include "../disc2-video/t210_regs.c"

#define SCAN_VSYNC 0x01
#define SCAN_PAGE 0x02
#define SCAN_BUSY 0x04
#define STEP_PAGE_REG6 0x10

// BUSY rises at VSYNC with PAGE flipping, whichever way the transfers before left it, is set again at the next VSYNC,
// and PAGE doesn't move until it ends
static void evaluate_regs(const void* data, uint32_t steps)
{
    const regs_t* result = (const regs_t*)data;
    int zero = 1;
    int busy = -1;
    int done = -1;
    int vsyncs = 0;
    int busy_at_second = 0;
    int page_kept = 1;
    (void)steps;

    for (int i = 0; i < REG6_FRAMES; i++)
        zero &= result->reg6_frames[i] == 0x00;

    for (int i = 1; i < SCAN_CHANGES && result->scan[i] != VIDEO_NO_CHANGE; i++)
    {
        uint32_t now = result->scan[i];
        uint32_t before = result->scan[i - 1];

        if ((now & SCAN_VSYNC) != 0 && (before & SCAN_VSYNC) == 0 && ++vsyncs == 2)
            busy_at_second = (now & SCAN_BUSY) != 0;

        if (busy < 0 && (now & SCAN_BUSY) != 0 && (before & SCAN_BUSY) == 0)
            busy = i;
        else if (busy >= 0 && done < 0 && (now & SCAN_BUSY) == 0)
            done = i;
        else if (busy >= 0 && done < 0 && ((now ^ before) & SCAN_PAGE) != 0)
            page_kept = 0;
    }

    check_true("REGISTER 6 READS DP1 WRITTEN", zero);
    check_true("PAGE FLIPS AS BUSY RISES", busy > 0 && ((result->scan[busy] ^ result->scan[busy - 1]) & SCAN_PAGE) != 0);
    check_true("BUSY SET AT THE NEXT VSYNC", busy_at_second);
    check_true("PAGE HELD UNTIL BUSY ENDS", busy > 0 && done > 0 && page_kept);

    if (check_marty())
    {
        check_range("128 ENTRIES, 15 KHZ, US", result->t128[1], 8800, 8930);
        check_range("256 ENTRIES, 15 KHZ, US", result->t256[1], 17580, 17730);

        if (busy > 0 && done > 0)
        {
            int32_t polls = (int32_t)(result->scan[done] >> 8) - (int32_t)(result->scan[busy] >> 8);
            check_signed("BUSY IN THE SCAN, US", video_poll_ns(polls, result->scan_elapsed, result->scan_done) / 1000,
                17500, 17850);
        }
    }
}

static const test_t k_test = CHECK_TEST(g_test_regs);

const check_group_t g_check_regs = { "SPRITE PAGE AND REGISTER 6", &k_test, STEP_PAGE_REG6, 0, evaluate_regs };
