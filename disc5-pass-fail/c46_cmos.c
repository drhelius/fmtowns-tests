#include "checks.h"
#include "../disc4-input-storage/t46_cmos.c"

// The backup RAM holds a byte at every address: what is there differs from one boot to the next, so only the writes
// and the reads that follow them are checked
static void evaluate_backup(const void* data, uint32_t steps)
{
    const backup_probe_t* probe = &((const cmos_t*)data)->probe;
    (void)steps;

    check_value("PROBE RAN", probe->ran, 1);
    check_hex("0486H", probe->port_0486, 0x0E);
    check_hex("EVEN BYTE READS ITS WRITE", probe->even_written, 0xA5);
    check_hex("ODD BYTE KEEPS ITS VALUE", probe->odd_byte, probe->found_odd);
    check_hex("WORD READ OF THE PAIR", probe->word, (uint32_t)(probe->found_odd << 8) | 0xA5);
    check_hex("DWORD READ, LOW WORD", probe->dword & 0xFFFF, probe->word);
    check_hex("ODD WRITE KEEPS THE EVEN BYTE", probe->even_after_odd, 0xA5);
    check_hex("ODD BYTE READS ITS WRITE", probe->odd_after_odd, 0x5A);
}

static void evaluate_window(const void* data, uint32_t steps)
{
    const cmos_t* result = (const cmos_t*)data;
    (void)steps;

    check_value("F40000H EQUALS 3000H PORTS", result->mismatches, 0);
}

static const test_t k_test = CHECK_TEST(g_test_cmos);

const check_group_t g_check_backup = { "BACKUP RAM BYTES (MARTY)", &k_test, 1, check_marty, evaluate_backup };
const check_group_t g_check_cmos = { "CMOS RAM WINDOW (MARTY)", &k_test, 1, check_marty, evaluate_window };
