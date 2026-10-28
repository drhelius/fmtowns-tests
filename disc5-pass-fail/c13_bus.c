#include "checks.h"
#include "../disc1-cpu-board/t13_bus.c"

// PIT ticks for 16,384 executions on the Marty, widened for the link address of this disc
typedef struct
{
    uint8_t loop;
    uint16_t minimum;
    uint16_t maximum;
} bus_window_t;

static const bus_window_t k_windows[] =
{
    { 1, 1170, 1250 }, { 2, 935, 1005 }, { 7, 3050, 3250 }, { 8, 2290, 2435 }, { 9, 3260, 3610 }, { 17, 7780, 8270 },
    { 19, 20700, 22000 }, { 24, 3700, 3940 }, { 29, 3370, 3650 }, { 42, 4880, 5190 }, { 47, 3690, 3995 },
    { 50, 3440, 3665 }, { 51, 18600, 19770 }, { 52, 18300, 19450 }, { 53, 4070, 4325 }, { 54, 4320, 4630 }
};

static uint32_t fastest(const bus_t* result, int loop)
{
    uint32_t best = 0xFFFFFFFFu;

    for (int i = 0; i < REPEATS; i++)
    {
        if (result->ticks[0][loop][i] < best)
            best = result->ticks[0][loop][i];
    }

    return best;
}

static void evaluate_bus(const void* data, uint32_t steps)
{
    const bus_t* result = (const bus_t*)data;
    (void)steps;

    for (unsigned i = 0; i < COUNT_OF(k_windows); i++)
        check_range(k_loops[k_windows[i].loop].name, fastest(result, k_windows[i].loop), k_windows[i].minimum,
            k_windows[i].maximum);

    check_range("006CH WAIT ON OUT, TICKS", fastest(result, 57) - fastest(result, 55), 2900, 3400);
    check_signed("006CH WAIT ON IN, TICKS", (int32_t)fastest(result, 56) - (int32_t)fastest(result, 54), -40, 40);
}

static const test_t k_test = CHECK_TEST(g_test_bus);

const check_group_t g_check_bus = { "BUS TIMING (MARTY)", &k_test, 1, check_marty, evaluate_bus };
