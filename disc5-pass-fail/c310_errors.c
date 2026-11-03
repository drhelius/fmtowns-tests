#include "checks.h"
#include "../disc3-sound-cd/t310_errors.c"
#include "cd_checks.h"

#define STEP_OUTSIDE 0x01
#define STEP_MALFORMED 0x02
#define STEP_UNKNOWN 0x08
#define STEP_SRST 0x10
#define STEP_DURING 0x20

static int last_is(const cd_keep_t* keep, uint8_t first, uint8_t second)
{
    int count = keep->count < CD_KEEP ? keep->count : CD_KEEP;
    return count > 0 && keep_is(keep, count - 1, first, second);
}

static int refused(const cd_keep_t* keep)
{
    return keep->count == 1 && keep->packets[0].bytes[0] == 0x01;
}

static int count_first(const cd_keep_t* keep, uint8_t first)
{
    int count = 0;

    for (int i = 0; i < keep->count && i < CD_KEEP; i++)
        count += keep->packets[i].bytes[0] == first;

    return count;
}

// What the drive answers to commands it can't carry out: 21 80 once it gives up on a sector, one packet of 01 and
// the state for what it refuses; 80h leaves a read running, a seek replaces it, SRST stops CD-DA
static void evaluate_answers(const void* data, uint32_t steps)
{
    const errors_t* result = (const errors_t*)data;
    uint32_t unreadable = 0;
    uint32_t unknown = 0;
    int command_86 = 0;
    int seek = keep_find(&result->during[1], 0x04);
    (void)steps;

    for (int i = 0; i < OUTSIDE - 1; i++)
        unreadable += last_is(&result->outside[i], 0x21, 0x80);

    for (int i = 0; i < UNKNOWN; i++)
    {
        const unknown_t* entry = &result->unknown[i];

        if (entry->command == 0xA6)
            command_86 = entry->count == 1 && entry->bytes[0][0] == 0x00;
        else if (entry->command != 0x3F)
            unknown += entry->count == 1 && entry->bytes[0][0] == 0x01;
    }

    check_value("UNREADABLE SECTORS: 21 80", unreadable, OUTSIDE - 1);
    check_true("START AFTER END: 01 ALONE", refused(&result->outside[OUTSIDE - 1]));
    check_true("BAD MSF DIGITS: 01 ALONE", refused(&result->malformed[0]) && refused(&result->malformed[1]));
    check_true("00:00:00 TAKEN, NOTHING AFTER", result->malformed[2].count == 1 &&
        result->malformed[2].packets[0].bytes[0] == 0x00);
    check_value("UNKNOWN COMMANDS: 01 ALONE", unknown, UNKNOWN - 2);
    check_true("86H: 00 AND THE STATE", command_86);
    check_true("1FH P0=3: 00 ALONE", result->special[0].count == 1 && result->special[0].packets[0].bytes[0] == 0x00);
    check_true("9FH: 01 AND THE STATE", refused(&result->special[1]));
    check_true("SRST STOPS CD-DA", result->srst[3].before != 0 && result->srst[3].after <= result->srst[3].before + 37);
    check_true("80H DURING A READ: IT GOES ON", count_first(&result->during[0], 0x22) >= 2);
    check_true("SEEK DURING A READ: 04 01", seek >= 0 && keep_is(&result->during[1], seek, 0x04, 0x01) &&
        count_first(&result->during[1], 0x22) == 0);
}

static const test_t k_test = CHECK_TEST(g_test_errors);

const check_group_t g_check_cd_answers = { "CDC ANSWERS (MARTY)", &k_test,
    STEP_OUTSIDE | STEP_MALFORMED | STEP_UNKNOWN | STEP_SRST | STEP_DURING, check_marty, evaluate_answers };
