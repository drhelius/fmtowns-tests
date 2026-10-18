#include "tests.h"

// 1.1 Register census (docs/12): every register a model might have, read once in a fixed order
// Ports whose reads change other tests' state (keyboard, CDC, FDC data, PIC, PIT, DMA) are left to their own tests

#define CENSUS_BYTES 69
#define CENSUS_WORDS 4
#define TIMER_SAMPLES 1000
#define PIT_WINDOW 3072

static const uint16_t k_ports[CENSUS_BYTES] =
{
    0x0022, 0x0024, 0x0025, 0x0026, 0x0027, 0x0030, 0x0031, 0x0034, 0x0060, 0x0068, 0x006A, 0x006B, 0x00C0, 0x00C2,
    0x0208, 0x020D, 0x0470, 0x0471, 0x0472, 0x0473, 0x0474, 0x0475, 0x0476, 0x0477, 0x0480, 0x0486, 0x0487, 0x0488,
    0x048A, 0x0490, 0x0491, 0x0492, 0x0494, 0x0496, 0x04C8, 0x0510, 0x0511, 0x0512, 0x0513, 0x0514, 0x0515, 0x0516,
    0x0517, 0x0518, 0x0519, 0x051A, 0x051B, 0x051C, 0x051D, 0x051E, 0x051F, 0x05E0, 0x05E2, 0x05E6, 0x05E8, 0x05EA,
    0x05EC, 0x05ED, 0x05EE, 0x0610, 0x0C30, 0x0C31, 0x0C32, 0x0C33, 0x0C34, 0xFDA2, 0xFDA4, 0x0B70, 0x0F00
};

static const uint16_t k_word_ports[CENSUS_WORDS] = { 0x0026, 0x0030, 0x006A, 0x0490 };

typedef struct
{
    uint32_t boot_cr0;
    uint32_t boot_eflags;
    uint8_t boot_0020;
    uint8_t boot_0028;
    uint8_t boot_05c0;
    uint8_t boot_05c2;
    uint8_t bytes[CENSUS_BYTES];
    uint8_t reserved[3];
    uint16_t words[CENSUS_WORDS];
    uint16_t timer_first;
    uint16_t timer_second;
    uint32_t pit_ticks;
    uint16_t word_min_step;
    uint16_t word_max_step;
    uint16_t word_backwards;
    uint16_t byte_min_step;
    uint16_t byte_max_step;
    uint16_t byte_backwards;
    uint8_t nmi_before;
    uint8_t nmi_written;
    uint8_t nmi_after;
    uint8_t nmi_restored;
    uint8_t serial[32];
    uint32_t cr0_before;
    uint32_t cr0_toggled;
    uint32_t cr0_restored;
    uint8_t speed_tested;
    uint8_t speed_05e2[2];
    uint8_t speed_05e6[2];
    uint8_t reserved2[3];
} census_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void measure_timer(census_t* result);
static void read_serial_rom(census_t* result);
static uint32_t timer_ns_per_count(const census_t* result);
static int et_kind(const census_t* result);

const test_t g_test_census =
{
    "1.1", "REGISTER CENSUS", 0, TEST_RUN_ALL, 1, sizeof(census_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    census_t* result = (census_t*)data;
    (void)steps;

    shell_step("BOOT VALUES");
    result->boot_cr0 = g_boot.cr0;
    result->boot_eflags = g_boot.eflags;
    result->boot_0020 = g_boot.port_0020;
    result->boot_0028 = g_boot.port_0028;
    result->boot_05c0 = g_boot.port_05c0;
    result->boot_05c2 = g_boot.port_05c2;

    shell_step("PORT READS");

    for (int i = 0; i < CENSUS_BYTES; i++)
        result->bytes[i] = in8(k_ports[i]);

    for (int i = 0; i < CENSUS_WORDS; i++)
        result->words[i] = in16(k_word_ports[i]);

    shell_step("0026H RATE");
    measure_timer(result);

    shell_step("0028H WRITE");
    result->nmi_before = in8(0x0028);
    result->nmi_written = result->nmi_before ^ 0x01;
    out8(0x0028, result->nmi_written);
    result->nmi_after = in8(0x0028);
    out8(0x0028, result->nmi_before);
    result->nmi_restored = in8(0x0028);

    shell_step("SERIAL ROM");
    read_serial_rom(result);

    shell_step("CR0 ET");
    result->cr0_before = read_cr0();
    write_cr0(result->cr0_before ^ 0x10);
    result->cr0_toggled = read_cr0();
    write_cr0(result->cr0_before);
    result->cr0_restored = read_cr0();

    shell_step("SPEED REGISTERS");

    if (g_boot.port_05e2 != 0xFF || g_boot.port_05e6 != 0xFF)
    {
        result->speed_tested = 1;
        result->speed_05e2[0] = in8(0x05E2);
        result->speed_05e6[0] = in8(0x05E6);
        out8(0x05E2, result->speed_05e2[0]);
        out8(0x05E6, result->speed_05e6[0]);
        result->speed_05e2[1] = in8(0x05E2);
        result->speed_05e6[1] = in8(0x05E6);
    }
}

// The rate over 10 ms of PIT counter 0, then the steps between back-to-back reads as words and as byte pairs
static void measure_timer(census_t* result)
{
    uint16_t pit_start = tb_pit_read();
    result->timer_first = in16(0x0026);

    while ((uint16_t)(pit_start - tb_pit_read()) < PIT_WINDOW)
        ;

    result->timer_second = in16(0x0026);
    result->pit_ticks = (uint16_t)(pit_start - tb_pit_read());

    uint16_t previous = in16(0x0026);
    result->word_min_step = 0xFFFF;

    for (int i = 0; i < TIMER_SAMPLES; i++)
    {
        uint16_t now = in16(0x0026);
        uint16_t step = (uint16_t)(now - previous);

        if (step > 0x8000)
            result->word_backwards++;
        else
        {
            if (step < result->word_min_step)
                result->word_min_step = step;

            if (step > result->word_max_step)
                result->word_max_step = step;
        }

        previous = now;
    }

    previous = (uint16_t)(in8(0x0026) | (in8(0x0027) << 8));
    result->byte_min_step = 0xFFFF;

    for (int i = 0; i < TIMER_SAMPLES; i++)
    {
        uint16_t now = (uint16_t)(in8(0x0026) | (in8(0x0027) << 8));
        uint16_t step = (uint16_t)(now - previous);

        if (step > 0x8000)
            result->byte_backwards++;
        else
        {
            if (step < result->byte_min_step)
                result->byte_min_step = step;

            if (step > result->byte_max_step)
                result->byte_max_step = step;
        }

        previous = now;
    }

    tb_sync();
}

// Chip select (bit 5) low, ID RESET (bit 7) falling rewinds, ID CLK (bit 6) rising advances, data in bit 0
static void read_serial_rom(census_t* result)
{
    out8(0x0032, 0x80);
    (void)in8(0x0032);
    out8(0x0032, 0x00);

    for (int bit = 0; bit < 256; bit++)
    {
        (void)in8(0x0032);
        uint8_t value = in8(0x0032) & 0x01;
        result->serial[31 - (bit >> 3)] |= (uint8_t)(value << (bit & 7));
        out8(0x0032, 0x40);
        (void)in8(0x0032);
        out8(0x0032, 0x00);
    }
}

static uint32_t timer_ns_per_count(const census_t* result)
{
    uint32_t counts = (uint16_t)(result->timer_second - result->timer_first);

    if (counts == 0)
        return 0;

    return result->pit_ticks * 625000u / (192u * counts);
}

static int et_kind(const census_t* result)
{
    uint32_t before = (result->cr0_before >> 4) & 1;
    uint32_t after = (result->cr0_toggled >> 4) & 1;
    return before != after ? 2 : (int)before;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 3;
}

static void page(const void* data, uint32_t steps, int number)
{
    const census_t* result = (const census_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_WHITE, "MODEL %s ID %04X %s", machine_model_name(g_machine.model), g_machine.id,
            machine_cpu_name(g_machine.cpu));
        shell_line(row++, COLOR_WHITE, "BOOT %s 05E8H %02X = %u MB%s", g_boot.boot_device == 8 ? "CD" : "FD",
            g_boot.port_05e8, g_machine.ram_mb, g_boot.port_05e8 == 0xFF ? " PROBED" : "");
        shell_line(row++, COLOR_WHITE, "CR0 AT BOOT %08X", result->boot_cr0);
        shell_line(row++, COLOR_WHITE, "EFLAGS AT BOOT %08X", result->boot_eflags);
        shell_line(row++, COLOR_WHITE, "0020H %02X 0028H %02X 05C0H %02X 05C2H %02X", result->boot_0020,
            result->boot_0028, result->boot_05c0, result->boot_05c2);
        row++;
        shell_line(row++, COLOR_CYAN, "SERIAL ROM, ROM ORDER");

        for (int line = 0; line < 4; line++)
        {
            const uint8_t* bytes = &result->serial[line * 8];
            shell_line(row++, COLOR_WHITE, "%02X %02X%02X%02X%02X %02X%02X%02X%02X", line * 8, bytes[0], bytes[1],
                bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]);
        }

        return;
    }

    if (number == 1)
    {
        for (int i = 0; i < CENSUS_BYTES; i++)
            scr_printf(SCREEN_LEFT + (i & 3) * 9, row + i / 4, COLOR_WHITE, "%04X %02X", k_ports[i], result->bytes[i]);

        return;
    }

    for (int i = 0; i < CENSUS_WORDS; i++)
    {
        scr_printf(SCREEN_LEFT + (i & 1) * 18, row + i / 2, COLOR_WHITE, "IN AX,%04X %04X", k_word_ports[i],
            result->words[i]);
    }

    row += 3;

    uint32_t rate = timer_ns_per_count(result);

    if (rate == 0)
        shell_line(row++, COLOR_WHITE, "0026H FIXED %04X = NO TIMER", result->timer_first);
    else
        shell_line(row++, COLOR_WHITE, "0026H %u.%03u US/COUNT OVER %u TK", rate / 1000, rate % 1000,
            result->pit_ticks);

    shell_line(row++, COLOR_WHITE, "WORD STEPS %u-%u BACK %u", result->word_min_step, result->word_max_step,
        result->word_backwards);
    shell_line(row++, COLOR_WHITE, "BYTE STEPS %u-%u BACK %u", result->byte_min_step, result->byte_max_step,
        result->byte_backwards);
    shell_line(row++, COLOR_WHITE, "%s", result->byte_backwards != 0 ? "BYTE PAIRS TEAR, WORD READ LATCHES" :
        "NO TEARING SEEN");
    row++;

    shell_line(row++, COLOR_WHITE, "0028H %02X WROTE %02X READ %02X BACK %02X", result->nmi_before,
        result->nmi_written, result->nmi_after, result->nmi_restored);
    shell_line(row++, COLOR_WHITE, "%s", result->nmi_after == 0xFF && result->nmi_before == 0xFF ? "0028H NOT DECODED" :
        result->nmi_after == result->nmi_written ? "0028H BIT 0 WRITABLE" : "0028H BIT 0 FIXED");

    static const char* const k_et[3] = { "ET FIXED 0", "ET FIXED 1", "ET WRITABLE" };

    row++;
    shell_line(row++, COLOR_WHITE, "CR0 %08X TOGGLED %08X", result->cr0_before, result->cr0_toggled);
    shell_line(row++, COLOR_WHITE, "%s", k_et[et_kind(result)]);
    row++;

    if (result->speed_tested)
        shell_line(row++, COLOR_WHITE, "05E2H %02X>%02X 05E6H %02X>%02X", result->speed_05e2[0], result->speed_05e2[1],
            result->speed_05e6[0], result->speed_05e6[1]);
    else
        shell_line(row++, COLOR_GRAY, "05E2H AND 05E6H READ FF, NOT WRITTEN");
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const census_t* result = (const census_t*)data;
    uint32_t rate = timer_ns_per_count(result);
    (void)steps;
    fmt_format(line, 34, "0024 %02X 0026 %u.%03u ET %u", result->bytes[1], rate / 1000, rate % 1000,
        (result->cr0_toggled >> 4) & 1);
    *color = COLOR_WHITE;
}
