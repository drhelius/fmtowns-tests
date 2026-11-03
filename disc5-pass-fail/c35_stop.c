#include "checks.h"
#include "../disc3-sound-cd/t35_stop.c"
#include "cd_checks.h"

#define STEP_STOP 0x01
#define STEP_STOPPED 0x02

// A stop ends with 11h once the disc has run down; a seek written meanwhile waits for it and drops that 11h
static void evaluate_stop(const void* data, uint32_t steps)
{
    const stop_t* result = (const stop_t*)data;
    const cd_keep_t* seek = &result->stopped[5];
    const cd_keep_t* after = &result->stopped[6];
    (void)steps;

    check_true("STOP WHILE PLAYING: 00 03", keep_is(&result->stop[0], 0, 0x00, 0x03));
    check_true("NEXT 80H: 00 05, THEN 11 05", keep_is(&result->stop[1], 0, 0x00, 0x05) &&
        keep_is(&result->stop[1], 1, 0x11, 0x05));
    check_true("STOP WHEN STOPPED: 00 05 ALONE", result->stopped[0].count == 1 &&
        keep_is(&result->stopped[0], 0, 0x00, 0x05));
    check_true("ITS 11 05 AFTER THE NEXT 80H", keep_is(&result->stopped[1], 0, 0x00, 0x05) &&
        keep_is(&result->stopped[1], 1, 0x11, 0x05));
    check_true("SEEK DURING A STOP: NO 11H", keep_find(seek, 0x11) < 0 && keep_find(after, 0x11) < 0);
    check_true("THEN 80H: 04 01, 00 01", keep_is(after, 0, 0x04, 0x01) && keep_is(after, 1, 0x00, 0x01));
}

static const test_t k_test = CHECK_TEST(g_test_stop);

const check_group_t g_check_cd_stop = { "CD-DA STOP (MARTY)", &k_test, STEP_STOP | STEP_STOPPED, check_marty,
    evaluate_stop };
