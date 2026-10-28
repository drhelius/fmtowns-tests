#include "checks.h"
#include "../disc1-cpu-board/t14_timer2.c"

// The Marty has it, so an emulated Marty without it fails
static int has_timer2(void)
{
    return g_machine.has_timer2 || check_marty();
}

static void evaluate_timer2(const void* data, uint32_t steps)
{
    const timer2_t* result = (const timer2_t*)data;
    int quiet = 1;
    int readback_ok = 1;
    (void)steps;

    check_value("TIMER II FOUND", result->present, 1);

    if (!result->present)
        return;

    for (int level = 1; level < 16; level++)
    {
        if (level != 11 && (result->enabled[level] != 0 || result->disabled[level] != 0))
            quiet = 0;
    }

    for (int i = 0; i < 16; i++)
        readback_ok &= result->readback[i] == 1000;

    check_value("WAITS TIMED OUT", result->timeouts, 0);
    check_value("IRQ LEVEL", result->level, 0);
    check_range("IRQS IN 100 MS, ENABLED", result->enabled[0], 95, 105);
    check_value("IRQS IN 100 MS, DISABLED", result->disabled[0], 0);
    check_true("NO IRQ ON THE OTHER LEVELS", quiet);
    check_value("REQUEST SEEN", result->hold_seen, 1);
    check_value("REQUEST HELD UNTIL READ", result->hold_held, 1);
    check_hex("0068H WITH A REQUEST", result->hold_status, 0x60);
    check_value("REQUEST AFTER THE READ", result->hold_after_read, 0);
    check_hex("NO FLAGS WHILE DISABLED", result->gate_status & 0x60, 0x00);
    check_value("IRQ AFTER ENABLING", result->stale_irq, 1);
    check_range("FIRST IRQ AFTER ENABLING, US", result->stale_us, 1000, 1100);
    check_value("PENDING BEFORE DISABLING", result->pending_before, 1);
    check_value("PENDING AFTER DISABLING", result->pending_after, 0);
    check_range("STARTS AT ENABLE, US", result->starts[START_ENABLE].minimum, 950, 1100);
    check_range("STARTS AT ENABLE, LATEST, US", result->starts[START_ENABLE].maximum, 950, 1100);
    check_range("NEW PERIOD WAITS, US", result->starts[START_NEW_PERIOD].minimum, 1900, 2100);
    check_range("NEW PERIOD WAITS, LATEST, US", result->starts[START_NEW_PERIOD].maximum, 1900, 2100);
    check_range("TEN PERIODS OF 1000 US", result->ten_periods, 9900, 10100);
    check_true("006AH/006BH READ THE PERIOD", readback_ok);
}

static const test_t k_test = CHECK_TEST(g_test_timer2);

const check_group_t g_check_timer2 = { "INTERVAL TIMER II", &k_test, 1, has_timer2, evaluate_timer2 };
