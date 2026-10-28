#include "checks.h"
#include "../disc2-video/t24_crtc.c"

static const char* const k_join_names[MODES] = { "15K: START 1 JOINS THE FIELD", "31K: START 1 JOINS THE FIELD" };
static const char* const k_irq_names[MODES] = { "15K: IRQ11 WITH THE VSYNC RISE", "31K: IRQ11 WITH THE VSYNC RISE" };
static const char* const k_vsync_names[MODES] = { "15K: VSYNCS IN 100 MS STOPPED", "31K: VSYNCS IN 100 MS STOPPED" };
static const char* const k_irq11_names[MODES] = { "15K: IRQ11S IN 100 MS STOPPED", "31K: IRQ11S IN 100 MS STOPPED" };
static const char* const k_hsync_names[MODES] = { "15K: HSYNCS STOPPED, PER MILLE", "31K: HSYNCS STOPPED, PER MILLE" };
static const char* const k_both_names[MODES] = { "15K: SYNC BITS SEEN BOTH WAYS", "31K: SYNC BITS SEEN BOTH WAYS" };
static const char* const k_running_names[MODES] = { "15K: VSYNCS IN 100 MS RUNNING", "31K: VSYNCS IN 100 MS RUNNING" };

// The first VSYNC after START goes back to 1 comes where the field would have been anyway, two fields after the last
// one before the stop: the sync never stopped
static void evaluate_start(const void* data, uint32_t steps)
{
    const crtc_result_t* result = (const crtc_result_t*)data;
    (void)steps;

    for (int m = 0; m < MODES; m++)
    {
        uint32_t joined = 0;
        uint32_t together = 0;

        for (int k = 0; k < REPEATS; k++)
        {
            const start_t* start = &result->starts[m][k];
            int64_t field = (int64_t)start->phase * 1000 + start->first_rise;
            int32_t gap = start->first_irr - start->first_rise;

            joined += start->state == 0 && start->first_rise != NO_EDGE && field >= 33200000 && field <= 33500000;
            together += start->first_irr != NO_EDGE && gap >= -5000 && gap <= 5000;
        }

        check_value(k_join_names[m], joined, REPEATS);
        check_value(k_irq_names[m], together, REPEATS);
    }
}

static void evaluate_stopped(const void* data, uint32_t steps)
{
    const crtc_result_t* result = (const crtc_result_t*)data;
    (void)steps;

    for (int m = 0; m < MODES; m++)
    {
        const count_t* stopped = &result->stopped[m];
        uint32_t running = result->running[m].counts[1];

        check_range(k_vsync_names[m], stopped->counts[0], 5, 7);
        check_range(k_irq11_names[m], stopped->counts[2], 5, 7);
        check_range(k_hsync_names[m], running > 1000 ? stopped->counts[1] * 1000 / running : 0, 970, 1030);
        check_hex(k_both_names[m], (stopped->counts[3] << 4) | stopped->counts[4], 0x30);
        check_range(k_running_names[m], result->running[m].counts[0], 6, 8);
    }
}

static const test_t k_test = CHECK_TEST(g_test_crtc);

const check_group_t g_check_start = { "CRTC START, FREE FIELD (MARTY)", &k_test, STEP_START, check_marty,
    evaluate_start };
const check_group_t g_check_stopped = { "SYNC WITH START 0 (MARTY)", &k_test, STEP_STOPPED, check_marty,
    evaluate_stopped };
