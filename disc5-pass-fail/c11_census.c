#include "checks.h"
#include "../disc1-cpu-board/t11_census.c"

static int has_0026(void)
{
    return tb_source() == TB_SOURCE_0026 || check_marty();
}

static void evaluate_0026(const void* data, uint32_t steps)
{
    const census_t* result = (const census_t*)data;
    (void)steps;

    check_range("NS A COUNT AGAINST THE PIT", timer_ns_per_count(result), 990, 1010);
    check_value("WORD READS GOING BACKWARDS", result->word_backwards, 0);
}

static void evaluate_cr0(const void* data, uint32_t steps)
{
    const census_t* result = (const census_t*)data;
    (void)steps;

    check_hex("CR0 IN PROTECTED MODE", result->cr0_before, 0x7FFFFFF1);
    check_hex("CR0 AFTER FLIPPING ET", result->cr0_toggled, 0x7FFFFFF1);
}

static const test_t k_test = CHECK_TEST(g_test_census);

const check_group_t g_check_0026 = { "0026H 1 MHZ COUNTER", &k_test, 1, has_0026, evaluate_0026 };
const check_group_t g_check_cr0 = { "CR0 RESERVED BITS (386SX)", &k_test, 1, check_386sx, evaluate_cr0 };
