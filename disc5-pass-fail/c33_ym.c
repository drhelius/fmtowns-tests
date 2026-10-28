#include "checks.h"
#include "../disc3-sound-cd/t33_ym.c"

#define STEP_BUSY 0x02
#define STEP_FLAGS 0x04
#define STEP_LATCH 0x10
#define STEP_TIMERS 0x20
#define STEP_ALONE 0x40

static const uint8_t k_latch[5] = { 0x01, 0x01, 0x01, 0x01, 0x03 };
static const uint8_t k_flags[4] = { 0x00, 0x01, 0x01, 0x01 };

static int bytes_equal(const uint8_t* a, const uint8_t* b, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (a[i] != b[i])
            return 0;
    }

    return 1;
}

// 04DAh, 04DCh and 04DEh read the status 04D8h latched, only 04D8h reads it live
static void evaluate_latch(const void* data, uint32_t steps)
{
    const ym_t* result = (const ym_t*)data;
    uint32_t matching = 0;
    (void)steps;

    check_true("04D8H LATCHES, 04DCH READS IT", bytes_equal(result->latch, k_latch, 5));
    check_true("TIMER FLAG ON 04D8H ONLY LIVE", bytes_equal(result->flags, k_flags, 4));

    for (int write = 0; write < ALONE_WRITES; write++)
    {
        for (int latched = 0; latched < 2; latched++)
        {
            for (int port = 0; port < ALONE_PORTS; port++)
            {
                int data_write = (write & 1) != 0;
                uint8_t now = !data_write ? 0x00 : port == 0 || latched ? 0x80 : 0x00;
                uint8_t later = data_write && port != 0 && latched ? 0x80 : 0x00;
                const uint8_t* got = result->alone[write][latched][port];
                matching += (got[0] & 0x80) == now && (got[1] & 0x80) == later;
            }
        }
    }

    check_value("PORTS READ ALONE AFTER WRITES", matching, ALONE_WRITES * 2 * ALONE_PORTS);
}

static int32_t slack(void)
{
    return tb_source() == TB_SOURCE_0026 ? 0 : CHECK_PIT_SLACK;
}

static void evaluate_busy(const void* data, uint32_t steps)
{
    const ym_t* result = (const ym_t*)data;
    uint32_t data_busy = 0xFFFF;
    uint32_t address_busy = 0;
    int quiet = 1;
    (void)steps;

    for (int i = 0; i < REPEATS; i++)
    {
        data_busy = result->busy_data[i] < data_busy ? result->busy_data[i] : data_busy;
        address_busy = result->busy_address[i] > address_busy ? result->busy_address[i] : address_busy;
    }

    for (int i = 0; i < 4; i++)
        quiet &= result->after_address[i] == 0x00;

    check_value("BUSY WAITS TIMED OUT", result->busy_timeouts, 0);
    check_hex("BUSY AFTER AN ADDRESS WRITE", result->busy_first[0], 0x00);
    check_hex("BUSY AFTER A DATA WRITE", result->busy_first[1], 0x80);
    check_true("NO BUSY AFTER ADDRESS WRITES", quiet);
    check_true("ALL PORTS BUSY AFTER DATA", (result->after_data[0] & result->after_data[1] & result->after_data[2] &
        result->after_data[3] & 0x80) != 0);
    check_true("100 US LATER ONLY 04D8H CLEAR", (result->later[0] & result->later[1] & result->later[2] & 0x80) != 0 &&
        result->later[3] == 0x00);
    check_signed("BUSY AFTER DATA, US", (int32_t)data_busy - result->overhead, 20 - slack(), 32 + slack());
    check_signed("BUSY AFTER ADDRESS, US", (int32_t)address_busy - result->overhead, -6 - slack(), 6 + slack());
}

static void evaluate_timers(const void* data, uint32_t steps)
{
    const ym_t* result = (const ym_t*)data;
    (void)steps;

    check_value("TIMER WAITS TIMED OUT", result->flag_timeouts, 0);
    check_range("8 PERIODS OF TIMER A 0, US", result->total_a[0], 147160, 147750);
    check_range("8 PERIODS OF TIMER A 512, US", result->total_a[1], 73500, 73960);
    check_range("8 PERIODS OF TIMER A 1000, US", result->total_a[2], 3430 - slack(), 3480 + slack());
    check_range("8 PERIODS OF TIMER B 0, US", result->total_b[0], 588650, 591000);
    check_range("8 PERIODS OF TIMER B 255, US", result->total_b[1], 2258 - slack(), 2350 + slack());
}

static const test_t k_test = CHECK_TEST(g_test_ym);

const check_group_t g_check_ym_latch = { "YM3438 STATUS LATCH", &k_test, STEP_FLAGS | STEP_LATCH | STEP_ALONE, 0,
    evaluate_latch };
const check_group_t g_check_ym_busy = { "YM3438 BUSY FLAG", &k_test, STEP_BUSY, 0, evaluate_busy };
const check_group_t g_check_ym_timers = { "YM3438 TIMERS A AND B", &k_test, STEP_TIMERS, 0, evaluate_timers };
