#include "checks.h"
#include "../disc4-input-storage/t44_drive.c"

#define STEP_REQUESTS 0x04
#define STEP_SEEKS 0x10
#define TYPE1_MASK 0x1D

static const uint32_t k_seek_us[2][RATES] = { { 30027, 60027, 100025, 150022 }, { 60073, 120071, 200068, 300064 } };

static const char* const k_seek_names[2][RATES] =
{
    { "SEEK 10, 3 MS STEPS, US", "SEEK 10, 6 MS STEPS, US", "SEEK 10, 10 MS STEPS, US", "SEEK 10, 15 MS STEPS, US" },
    { "SEEK 10, 1 MHZ, 6 MS, US", "SEEK 10, 1 MHZ, 12 MS, US", "SEEK 10, 1 MHZ, 20 MS, US", "SEEK 10, 1 MHZ, 30 MS, US" }
};

static const char* const k_restore_names[2][RATES] =
{
    { "RESTORE OVER SEEK, 3 MS, US", "RESTORE OVER SEEK, 6 MS, US", "RESTORE OVER SEEK, 10 MS, US",
        "RESTORE OVER SEEK, 15 MS, US" },
    { "RESTORE OVER SEEK, 1 MHZ, US", "RESTORE OVER SEEK, 1 MHZ, US", "RESTORE OVER SEEK, 1 MHZ, US",
        "RESTORE OVER SEEK, 1 MHZ, US" }
};

// A Restore from track 10 ends 150.57 ms later: with IRQ6 masked, or IRQMSK set, the request still reaches the IRR,
// and the poll word reads 3Fh idle and BEh with it
static void evaluate_requests(const void* data, uint32_t steps)
{
    const drive_t* result = (const drive_t*)data;
    const request_log_t* requests = result->requests;
    (void)steps;

    check_true("IRR BIT 6, IRQ6 MASKED", requests[0].count == 1 && requests[0].changes[0].value == 0x40);
    check_range("REQUEST AFTER THE RESTORE, US", requests[0].changes[0].time, 150300, 150900);
    check_value("NO REQUEST WITH IRQMSK 0", requests[1].count, 0);
    check_true("IRR BIT 6, OPEN, IF 0", requests[2].count == 1 && requests[2].changes[0].value == 0x40);
    check_range("REQUEST, OPEN, IF 0, US", requests[2].changes[0].time, 150300, 150900);
    check_hex("POLL WORD, NOTHING REQUESTED", requests[3].first, 0x3F);
    check_value("POLL, MASKED: NO REQUEST", requests[3].count, 0);
    check_hex("POLL WORD WITH THE REQUEST", requests[4].changes[0].value, 0xBE);
    check_range("POLL, REQUEST AFTER, US", requests[4].changes[0].time, 150300, 150900);
    check_true("FORCE D8H REQUESTS AT ONCE", requests[5].count >= 1 && requests[5].changes[0].value == 0x40 &&
        requests[5].changes[0].time <= 200);
    check_hex("STATUS READ KEEPS THE REQUEST", requests[5].after_status, 0x40);
    check_hex("FORCE D0H CLEARS IT", requests[5].after_clear, 0x00);

    for (int i = 0; i < 5; i++)
    {
        if ((requests[i].status & TYPE1_MASK) != 0x04)
        {
            check_hex("STATUS AFTER THE RESTORE", requests[i].status & TYPE1_MASK, 0x04);
            break;
        }
    }
}

// Seeks of 10 tracks at each step rate, then a Restore takes 1,000 FDC clocks more than the Seek did
static void evaluate_seeks(const void* data, uint32_t steps)
{
    const seeks_t* seeks = &((const drive_t*)data)->seeks;
    int clocks = check_marty() ? 2 : 1;
    int32_t slack = tb_source() == TB_SOURCE_0026 ? 0 : CHECK_PIT_SLACK;
    int statuses = 1;
    (void)steps;

    for (int clock = 0; clock < clocks; clock++)
    {
        int32_t extra = clock == 0 ? 490 : 990;

        for (int rate = 0; rate < RATES; rate++)
        {
            uint32_t seek = seeks->seek_time[clock][rate];
            int32_t restore = (int32_t)seeks->restore_time[clock][rate] - (int32_t)seek;

            check_range(k_seek_names[clock][rate], seek, k_seek_us[clock][rate] - 30 - slack,
                k_seek_us[clock][rate] + 30 + slack);
            check_signed(k_restore_names[clock][rate], restore, extra - 20 - slack, extra + 20 + slack);
            statuses &= (seeks->seek_status[clock][rate] & TYPE1_MASK) == 0x00 &&
                (seeks->restore_status[clock][rate] & TYPE1_MASK) == 0x04;
        }
    }

    check_true("TRACK 0 ONLY AFTER RESTORES", statuses);
}

static const test_t k_test = CHECK_TEST(g_test_drive);

const check_group_t g_check_fdc_request = { "FDC REQUEST AND PIC POLL WORD", &k_test, STEP_REQUESTS, 0,
    evaluate_requests };
const check_group_t g_check_fdc_seeks = { "FDC SEEK AND RESTORE TIMES", &k_test, STEP_SEEKS, 0,
    evaluate_seeks };
