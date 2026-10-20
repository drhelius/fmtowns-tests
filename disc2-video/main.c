#include "tests.h"

static const test_t* const k_tests[] =
{
    &g_test_sync,
    &g_test_cards,
    &g_test_irq,
    &g_test_crtc,
    &g_test_lines,
    &g_test_wrap,
    &g_test_page,
    &g_test_midline,
    &g_test_sprite,
    &g_test_regs
};

static const disc_t k_disc = { 2, "VIDEO", "V1", k_tests, COUNT_OF(k_tests) };

void main(void)
{
    shell_main(&k_disc);
}
