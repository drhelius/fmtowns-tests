#include "tests.h"

static const test_t* const k_tests[] =
{
    &g_test_census,
    &g_test_memory,
    &g_test_bus,
    &g_test_timer2,
    &g_test_rtc,
    &g_test_protect,
    &g_test_flags,
    &g_test_exceptions,
    &g_test_fpu_id,
    &g_test_fpu
};

static const disc_t k_disc = { 1, "CPU BUS BOARD", "V1", k_tests, COUNT_OF(k_tests) };

void main(void)
{
    shell_main(&k_disc);
}
