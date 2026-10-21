#include "tests.h"

static const test_t* const k_tests[] =
{
    &g_test_buzzer,
    &g_test_ssg,
    &g_test_ym,
    &g_test_coupling,
    &g_test_stop,
    &g_test_pregap,
    &g_test_state,
    &g_test_timing,
    &g_test_transfer,
    &g_test_errors,
    &g_test_lid,
    &g_test_play,
    &g_test_irq
};

static const disc_t k_disc = { 3, "SOUND CD-ROM", "V1", k_tests, COUNT_OF(k_tests) };

void main(void)
{
    shell_main(&k_disc);
}
