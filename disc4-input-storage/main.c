#include "tests.h"

static const test_t* const k_tests[] =
{
    &g_test_pad,
    &g_test_mouse,
    &g_test_keyboard,
    &g_test_drive,
    &g_test_sectors,
    &g_test_cmos,
    &g_test_scsi,
    &g_test_ports,
    &g_test_card
};

static const disc_t k_disc = { 4, "INPUT STORAGE", "V1", k_tests, COUNT_OF(k_tests) };

void main(void)
{
    shell_main(&k_disc);
}
