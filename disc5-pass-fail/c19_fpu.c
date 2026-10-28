#include "checks.h"
#include "../disc1-cpu-board/t19_fpu.c"

static void check_esc(const esc_run_t* run, const char* faults, const char* status, const char* control,
    const char* status_ax)
{
    check_true(faults, run->fninit == 0 && run->fnstsw_m16 == 0 && run->fnstcw_m16 == 0 && run->fnstsw_ax == 0);
    check_hex(status, run->status_m16, 0xFFFF);
    check_hex(control, run->control_m16, 0xFFFF);
    check_hex(status_ax, run->status_ax, 0xFFFF);
}

static void evaluate_esc(const void* data, uint32_t steps)
{
    const fpu_id_t* result = (const fpu_id_t*)data;
    (void)steps;

    if (result->fpu_present)
    {
        check_skip("A COPROCESSOR ANSWERS");
        return;
    }

    check_esc(&result->esc[0], "MP 0: NO FAULT ON THE ESCS", "MP 0: FNSTSW M16 STORES", "MP 0: FNSTCW M16 STORES",
        "MP 0: FNSTSW AX GIVES");
    check_esc(&result->esc[1], "MP 1: NO FAULT ON THE ESCS", "MP 1: FNSTSW M16 STORES", "MP 1: FNSTCW M16 STORES",
        "MP 1: FNSTSW AX GIVES");
    check_value("EM 1: FNINIT RAISES VECTOR", result->nm_em, CPU_VECTOR_NM + 1);
    check_value("TS 1: FNINIT RAISES VECTOR", result->nm_ts, CPU_VECTOR_NM + 1);
    check_value("MP 1 TS 1: WAIT RAISES VECTOR", result->nm_wait, CPU_VECTOR_NM + 1);

    if (check_marty())
    {
        check_range("FNINIT, PIT TICKS FOR 64", result->ticks_fninit, 26, 32);
        check_range("FNSTSW AX, PIT TICKS FOR 64", result->ticks_fnstsw, 26, 32);
        check_range("NOP, PIT TICKS FOR 64", result->ticks_nop, 4, 6);
    }
}

static const test_t k_test = CHECK_TEST(g_test_fpu_id);

const check_group_t g_check_esc = { "ESC WITH NO COPROCESSOR", &k_test, 0x0F, check_386, evaluate_esc };
