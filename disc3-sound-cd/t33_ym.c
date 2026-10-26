#include "tests.h"

// 3.3 YM3438 read ports and timers (docs/12, HT-14): what 04DAh, 04DCh and 04DEh read next to the status at 04D8h,
// whether 04DCh latches, how long a latched value holds, the busy time after an address and a data write, and the
// timer periods, which give the chip's clock. Every write here waits a fixed time instead of reading the busy flag,
// so no 04D8h read happens unless the step makes it. Step 7 reads each port alone after an address or a data write to
// either bank, right away and 100 us later, with and without a 04D8h read first

#define REPEATS 8
#define HOLD_POINTS 6
#define PERIODS 8
#define TIMER_A_CASES 3
#define TIMER_B_CASES 2
#define WRITE_GAP 50
#define BUSY_POLLS 100000u
#define FLAG_TIMEOUT 200000u
#define LFO_REGISTER 0x22
#define TIMER_CONTROL 0x27
#define ALONE_WRITES 4
#define ALONE_PORTS 4
#define BANK1_REGISTER 0xA4

typedef struct
{
    uint8_t boot[4];
    uint8_t idle[16];
    uint8_t idle_ports[4];
    uint8_t after_address[4];
    uint8_t after_data[4];
    uint8_t later[4];
    uint8_t busy_first[2];
    uint8_t busy_timeouts;
    uint8_t flag_timeouts;
    uint16_t busy_address[REPEATS];
    uint16_t busy_data[REPEATS];
    uint16_t overhead;
    uint16_t reserved16;
    uint8_t flags[4];
    uint8_t hold_latched;
    uint8_t hold_live;
    uint8_t hold[HOLD_POINTS][2];
    uint8_t latch[5];
    uint8_t reserved[3];
    uint32_t total_a[TIMER_A_CASES];
    uint32_t shortest_a[TIMER_A_CASES];
    uint32_t longest_a[TIMER_A_CASES];
    uint32_t total_b[TIMER_B_CASES];
    uint32_t shortest_b[TIMER_B_CASES];
    uint32_t longest_b[TIMER_B_CASES];
    uint8_t alone[ALONE_WRITES][2][ALONE_PORTS][2];
} ym_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void measure_boot(ym_t* result);
static void measure_busy(ym_t* result);
static void measure_flags(ym_t* result);
static void measure_hold(ym_t* result);
static void measure_latch(ym_t* result);
static void measure_timers(ym_t* result);
static void measure_alone(ym_t* result);
static void alone_case(int write, int latched, uint16_t port, uint8_t* reads);
static uint16_t busy_time(ym_t* result, int data, uint8_t* first);
static void timer_period(ym_t* result, int b, uint16_t value, uint32_t* total, uint32_t* shortest, uint32_t* longest);
static void start_timers(uint16_t a, uint8_t b, uint8_t control);
static void read_ports(uint8_t* values, const uint16_t* ports, int count);
static uint32_t clock_khz(uint32_t total, uint32_t units);
static const char* port_verdict(uint8_t value, uint8_t live, uint8_t latch);

static const test_step_t k_steps[] =
{
    { "BOOT AND IDLE READS", 0 },
    { "BUSY AFTER WRITES", 0 },
    { "TIMER FLAG READS", 0 },
    { "LATCH HOLD TIME 60 S", 0 },
    { "04DCH LATCH", 0 },
    { "TIMER PERIODS", 0 },
    { "PORTS ALONE AFTER WRITES", 0 },
    { 0, 0 }
};

static const uint16_t k_after[4] = { 0x04D8, 0x04DA, 0x04DE, 0x04DC };
static const uint16_t k_later[4] = { 0x04DA, 0x04DE, 0x04DC, 0x04D8 };
static const uint16_t k_flag_reads[4] = { 0x04DA, 0x04D8, 0x04DA, 0x04DE };
static const uint32_t k_hold_us[HOLD_POINTS] = { 1000, 100000, 1000000, 10000000, 30000000, 60000000 };
static const char* const k_hold_names[HOLD_POINTS] = { "1 MS", "100 MS", "1 S", "10 S", "30 S", "60 S" };
static const uint16_t k_timer_a[TIMER_A_CASES] = { 0, 512, 1000 };
static const uint8_t k_timer_b[TIMER_B_CASES] = { 0, 255 };
static const uint16_t k_alone_ports[ALONE_PORTS] = { 0x04D8, 0x04DA, 0x04DC, 0x04DE };
static const char* const k_alone_names[ALONE_WRITES] = { "A0", "D0", "A1", "D1" };

const test_t g_test_ym =
{
    "3.3", "YM3438 READ PORTS", 0, TEST_RUN_ALL, 2, sizeof(ym_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    ym_t* result = (ym_t*)data;

    if ((steps & 0x01) != 0)
        measure_boot(result);

    if ((steps & 0x02) != 0)
        measure_busy(result);

    if ((steps & 0x04) != 0)
        measure_flags(result);

    if ((steps & 0x08) != 0)
        measure_hold(result);

    if ((steps & 0x10) != 0)
        measure_latch(result);

    if ((steps & 0x20) != 0)
        measure_timers(result);

    if ((steps & 0x40) != 0)
        measure_alone(result);

    snd_ym(0, TIMER_CONTROL, 0x30);
}

static void measure_boot(ym_t* result)
{
    static const uint16_t k_ports[4] = { 0x04DA, 0x04DC, 0x04DE, 0x04D8 };

    shell_step("BOOT AND IDLE READS");
    result->boot[0] = g_boot.port_04da;
    result->boot[1] = g_boot.port_04de;
    result->boot[2] = g_boot.port_04dc;
    result->boot[3] = g_boot.port_04d8;
    read_ports(result->idle_ports, k_ports, 4);

    for (int i = 0; i < 16; i++)
        result->idle[i] = in8(0x04D8);
}

static void measure_busy(ym_t* result)
{
    shell_step("BUSY AFTER WRITES");
    out8(0x04D8, LFO_REGISTER);
    read_ports(result->after_address, k_after, 4);
    tb_delay(WRITE_GAP);
    out8(0x04DA, 0x00);
    read_ports(result->after_data, k_after, 4);
    tb_delay(100);
    read_ports(result->later, k_later, 4);
    tb_delay(WRITE_GAP);

    result->overhead = 0xFFFF;

    for (int i = 0; i < REPEATS; i++)
    {
        uint32_t start = tb_now();
        (void)in8(0x04D8);

        for (uint32_t polls = 0; (in8(0x04D8) & 0x80) != 0 && polls < BUSY_POLLS; polls++)
            ;

        uint32_t time = tb_now() - start;
        result->overhead = time < result->overhead ? (uint16_t)time : result->overhead;
    }

    for (int i = 0; i < REPEATS; i++)
    {
        result->busy_address[i] = busy_time(result, 0, &result->busy_first[0]);
        tb_delay(WRITE_GAP);
        result->busy_data[i] = busy_time(result, 1, &result->busy_first[1]);
        tb_delay(WRITE_GAP);
    }
}

// From the write to bit 7 of 04D8h falling; an address write alone, or a data write to the LFO register
static uint16_t busy_time(ym_t* result, int data, uint8_t* first)
{
    uint32_t polls = 0;

    if (data)
    {
        out8(0x04D8, LFO_REGISTER);
        tb_delay(WRITE_GAP);
    }

    uint32_t start = tb_now();
    out8(data ? 0x04DA : 0x04D8, data ? 0x00 : LFO_REGISTER);
    *first = in8(0x04D8);

    while ((in8(0x04D8) & 0x80) != 0 && polls < BUSY_POLLS)
        polls++;

    uint32_t end = tb_now();

    if (polls >= BUSY_POLLS)
    {
        result->busy_timeouts++;
        return 0xFFFF;
    }

    return (uint16_t)(end - start);
}

// Timer A overflows with its flag enabled; 04DAh first, then the status, then 04DAh and 04DEh again
static void measure_flags(ym_t* result)
{
    shell_step("TIMER FLAG READS");
    start_timers(1000, 0, 0x05);
    tb_delay(2000);
    read_ports(result->flags, k_flag_reads, 4);
    snd_ym(0, TIMER_CONTROL, 0x30);
}

// Both flags latched through 04D8h, then cleared through register 27h; 04DAh and 04DEh only from then on
static void measure_hold(ym_t* result)
{
    shell_step("LATCH HOLD: LATCHING");
    start_timers(1000, 255, 0x0F);
    tb_delay(2000);
    result->hold_latched = in8(0x04D8);
    snd_ym(0, TIMER_CONTROL, 0x30);
    uint32_t start = tb_now();

    for (int i = 0; i < HOLD_POINTS; i++)
    {
        shell_step("LATCH HOLD: READ AT %s", k_hold_names[i]);

        while (tb_now() - start < k_hold_us[i])
            ;

        result->hold[i][0] = in8(0x04DA);
        result->hold[i][1] = in8(0x04DE);
    }

    result->hold_live = in8(0x04D8);
}

// 01h latched through 04D8h, then 03h set and 04DCh read: 04DAh shows which one the latch holds
static void measure_latch(ym_t* result)
{
    static const uint16_t k_ports[5] = { 0x04D8, 0x04DC, 0x04DA, 0x04DE, 0x04D8 };

    shell_step("04DCH LATCH");
    start_timers(1000, 0, 0x05);
    tb_delay(2000);
    result->latch[0] = in8(k_ports[0]);
    start_timers(1000, 255, 0x0F);
    tb_delay(2000);
    read_ports(result->latch + 1, k_ports + 1, 4);
    snd_ym(0, TIMER_CONTROL, 0x30);
}

static void measure_timers(ym_t* result)
{
    for (int i = 0; i < TIMER_A_CASES; i++)
    {
        shell_step("TIMER A %u", k_timer_a[i]);
        timer_period(result, 0, k_timer_a[i], &result->total_a[i], &result->shortest_a[i], &result->longest_a[i]);
    }

    for (int i = 0; i < TIMER_B_CASES; i++)
    {
        shell_step("TIMER B %u", k_timer_b[i]);
        timer_period(result, 1, k_timer_b[i], &result->total_b[i], &result->shortest_b[i], &result->longest_b[i]);
    }
}

// Flag rise to flag rise; each rise resets the flag with one data write to 27h, which keeps the timer loaded
static void timer_period(ym_t* result, int b, uint16_t value, uint32_t* total, uint32_t* shortest, uint32_t* longest)
{
    uint8_t control = b ? 0x2A : 0x15;
    uint8_t flag = b ? 0x02 : 0x01;
    uint32_t previous = 0;

    *shortest = 0xFFFFFFFFu;
    *longest = 0;
    start_timers(b ? 0 : value, b ? (uint8_t)value : 0, (uint8_t)(control & 0x0F));
    out8(0x04D8, TIMER_CONTROL);
    tb_delay(WRITE_GAP);
    out8(0x04DA, control);

    for (int i = 0; i <= PERIODS; i++)
    {
        uint32_t start = tb_now();

        while ((in8(0x04D8) & flag) == 0)
        {
            if (tb_now() - start > FLAG_TIMEOUT)
            {
                result->flag_timeouts++;
                snd_ym(0, TIMER_CONTROL, 0x30);
                *total = 0;
                return;
            }
        }

        uint32_t now = tb_now();
        out8(0x04DA, control);

        if (i > 0)
        {
            uint32_t period = now - previous;
            *shortest = period < *shortest ? period : *shortest;
            *longest = period > *longest ? period : *longest;
            *total += period;
        }

        previous = now;
    }

    snd_ym(0, TIMER_CONTROL, 0x30);
}

static void measure_alone(ym_t* result)
{
    shell_step("PORTS ALONE AFTER WRITES");

    for (int write = 0; write < ALONE_WRITES; write++)
    {
        for (int latched = 0; latched < 2; latched++)
        {
            for (int port = 0; port < ALONE_PORTS; port++)
                alone_case(write, latched, k_alone_ports[port], result->alone[write][latched][port]);
        }
    }
}

// From idle with a clear latch: an address write (A) or an address and a data write (D) to bank 0 (LFO, 00h) or bank 1
// (channel 4's block and F-number, 00h), optionally a 04D8h read, then the port alone, now and 100 us later
static void alone_case(int write, int latched, uint16_t port, uint8_t* reads)
{
    uint16_t address = (write & 0x02) != 0 ? 0x04DC : 0x04D8;
    uint8_t reg = (write & 0x02) != 0 ? BANK1_REGISTER : LFO_REGISTER;

    tb_delay(WRITE_GAP);
    (void)in8(0x04D8);
    out8(address, reg);

    if ((write & 0x01) != 0)
    {
        tb_delay(WRITE_GAP);
        out8((uint16_t)(address + 2), 0x00);
    }

    if (latched)
        (void)in8(0x04D8);

    reads[0] = in8(port);
    tb_delay(100);
    reads[1] = in8(port);
}

static void start_timers(uint16_t a, uint8_t b, uint8_t control)
{
    snd_ym(0, TIMER_CONTROL, 0x30);
    snd_ym(0, 0x24, (uint8_t)(a >> 2));
    snd_ym(0, 0x25, (uint8_t)(a & 0x03));
    snd_ym(0, 0x26, b);
    snd_ym(0, TIMER_CONTROL, control);
}

static void read_ports(uint8_t* values, const uint16_t* ports, int count)
{
    for (int i = 0; i < count; i++)
        values[i] = in8(ports[i]);
}

// 144 chip clocks per Timer A unit, 2,304 per Timer B unit
static uint32_t clock_khz(uint32_t total, uint32_t units)
{
    uint32_t clocks = units * PERIODS;

    if (total == 0)
        return 0;

    return clocks / total * 1000u + clocks % total * 1000u / total;
}

static const char* port_verdict(uint8_t value, uint8_t live, uint8_t latch)
{
    if (value == latch && value != live)
        return "LATCH";

    if (value == live && value != latch)
        return "LIVE";

    if (value == 0xFF)
        return "FF";

    if (value == 0x00)
        return "00";

    return value == live ? "LIVE=LATCH" : "OTHER";
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 5;
}

static void page(const void* data, uint32_t steps, int number)
{
    const ym_t* result = (const ym_t*)data;
    int row = SHELL_BODY_TOP;

    if (number == 0)
    {
        uint32_t low = 0xFFFF;
        uint32_t high = 0;
        uint32_t low_data = 0xFFFF;
        uint32_t high_data = 0;

        shell_line(row++, COLOR_CYAN, "BOOT 04DA 04DE 04DC 04D8: %02X %02X %02X %02X", result->boot[0], result->boot[1],
            result->boot[2], result->boot[3]);
        shell_line(row++, COLOR_WHITE, "IDLE 04DA 04DC 04DE 04D8: %02X %02X %02X %02X", result->idle_ports[0],
            result->idle_ports[1], result->idle_ports[2], result->idle_ports[3]);
        shell_line(row++, COLOR_WHITE, "04D8H X16 %02X%02X%02X%02X %02X%02X%02X%02X", result->idle[0], result->idle[1],
            result->idle[2], result->idle[3], result->idle[4], result->idle[5], result->idle[6], result->idle[7]);
        shell_line(row++, COLOR_WHITE, "          %02X%02X%02X%02X %02X%02X%02X%02X", result->idle[8], result->idle[9],
            result->idle[10], result->idle[11], result->idle[12], result->idle[13], result->idle[14],
            result->idle[15]);
        row++;
        shell_line(row++, COLOR_CYAN, "READS RIGHT AFTER A WRITE");
        shell_line(row++, COLOR_GRAY, "           04D8 04DA 04DE 04DC");
        shell_line(row++, COLOR_WHITE, "ADDRESS     %02X   %02X   %02X   %02X", result->after_address[0],
            result->after_address[1], result->after_address[2], result->after_address[3]);
        shell_line(row++, COLOR_WHITE, "DATA        %02X   %02X   %02X   %02X", result->after_data[0],
            result->after_data[1], result->after_data[2], result->after_data[3]);
        shell_line(row++, COLOR_WHITE, "100 US      %02X   %02X   %02X   %02X", result->later[3], result->later[0],
            result->later[1], result->later[2]);
        row++;

        for (int i = 0; i < REPEATS; i++)
        {
            low = result->busy_address[i] < low ? result->busy_address[i] : low;
            high = result->busy_address[i] > high ? result->busy_address[i] : high;
            low_data = result->busy_data[i] < low_data ? result->busy_data[i] : low_data;
            high_data = result->busy_data[i] > high_data ? result->busy_data[i] : high_data;
        }

        shell_line(row++, COLOR_CYAN, "BUSY BIT, US FROM THE WRITE");
        shell_line(row++, COLOR_WHITE, "NO WRITE (LOOP ALONE) %3u", result->overhead);
        shell_line(row++, COLOR_WHITE, "ADDRESS WRITE %3u-%-3u FIRST %02X", low, high, result->busy_first[0]);
        shell_line(row++, COLOR_WHITE, "DATA WRITE    %3u-%-3u FIRST %02X", low_data, high_data, result->busy_first[1]);
        shell_line(row++, COLOR_GRAY, "24 US = 192 CLOCKS AT 8 MHZ");

        if (result->busy_timeouts != 0)
            shell_line(row++, COLOR_YELLOW, "BUSY NEVER FELL X%u", result->busy_timeouts);

        return;
    }

    if (number == 1)
    {
        shell_line(row++, COLOR_CYAN, "TIMER A FLAG SET, NO STATUS READ");
        shell_line(row++, COLOR_WHITE, "04DA %02X  04D8 %02X  04DA %02X  04DE %02X", result->flags[0], result->flags[1],
            result->flags[2], result->flags[3]);
        shell_line(row++, COLOR_WHITE, "04DAH AFTER 04D8H: %s", port_verdict(result->flags[2], result->flags[1],
            result->flags[1]));
        shell_line(row++, COLOR_GRAY, "LATCH 00 01 01 01, ZERO 00 01 00 00");
        shell_line(row++, COLOR_GRAY, "OPEN BUS FF 01 FF FF");
        row++;
        shell_line(row++, COLOR_CYAN, "04DCH: 01 LATCHED, 03 SET");
        shell_line(row++, COLOR_GRAY, "04D8 04DC 04DA 04DE 04D8");
        shell_line(row++, COLOR_WHITE, " %02X   %02X   %02X   %02X   %02X", result->latch[0], result->latch[1],
            result->latch[2], result->latch[3], result->latch[4]);
        shell_line(row++, COLOR_WHITE, "04DCH %s, 04DAH %s", port_verdict(result->latch[1], result->latch[4],
            result->latch[0]), port_verdict(result->latch[2], result->latch[4], result->latch[0]));
        shell_line(row++, COLOR_GRAY, "04DCH LIVE AND LATCHING: 04DAH 03");
        return;
    }

    if (number == 2)
    {
        shell_line(row++, COLOR_CYAN, "HOLD: 04D8H LATCHED %02X, CLEARED", result->hold_latched);

        for (int i = 0; i < HOLD_POINTS; i++)
            shell_line(row++, COLOR_WHITE, "%-6s 04DA %02X 04DE %02X", k_hold_names[i], result->hold[i][0],
                result->hold[i][1]);

        shell_line(row++, COLOR_WHITE, "04D8H AT THE END %02X", result->hold_live);
        shell_line(row++, COLOR_GRAY, "DIE MODEL: 03 UNTIL ~30 S, THEN 00");
        return;
    }

    if (number == 4)
    {
        shell_line(row++, COLOR_CYAN, "PORTS ALONE AFTER A WRITE, NOW/100US");

        if ((steps & 0x40) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        shell_line(row++, COLOR_GRAY, "WRITE   04D8  04DA  04DC  04DE");

        for (int write = 0; write < ALONE_WRITES; write++)
        {
            for (int latched = 0; latched < 2; latched++)
            {
                const uint8_t(*reads)[2] = result->alone[write][latched];
                shell_line(row++, COLOR_WHITE, "%s %s  %02X/%02X %02X/%02X %02X/%02X %02X/%02X", k_alone_names[write],
                    latched ? "L" : "-", reads[0][0], reads[0][1], reads[1][0], reads[1][1], reads[2][0], reads[2][1],
                    reads[3][0], reads[3][1]);
            }
        }

        shell_line(row++, COLOR_GRAY, "A ADDRESS, D ADDRESS+DATA, BANK 0/1;");
        shell_line(row++, COLOR_GRAY, "L: 04D8H READ BEFORE THE PORT");
        return;
    }

    shell_line(row++, COLOR_CYAN, "TIMER PERIODS, 8 EACH, US");
    shell_line(row++, COLOR_GRAY, "TIMER  N   MEAN  MIN-MAX    CLOCK KHZ");

    for (int i = 0; i < TIMER_A_CASES; i++)
    {
        uint32_t units = 144u * (1024u - k_timer_a[i]);
        shell_line(row++, COLOR_WHITE, "A  %4u %6u %6u-%-6u %5u", k_timer_a[i], result->total_a[i] / PERIODS,
            result->shortest_a[i], result->longest_a[i], clock_khz(result->total_a[i], units));
    }

    for (int i = 0; i < TIMER_B_CASES; i++)
    {
        uint32_t units = 2304u * (256u - k_timer_b[i]);
        shell_line(row++, COLOR_WHITE, "B  %4u %6u %6u-%-6u %5u", k_timer_b[i], result->total_b[i] / PERIODS,
            result->shortest_b[i], result->longest_b[i], clock_khz(result->total_b[i], units));
    }

    shell_line(row++, COLOR_GRAY, "8000 KHZ: A 18 US PER UNIT,");
    shell_line(row++, COLOR_GRAY, "B 288 US PER UNIT");

    if (result->flag_timeouts != 0)
        shell_line(row++, COLOR_YELLOW, "FLAG NEVER CAME X%u", result->flag_timeouts);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const ym_t* result = (const ym_t*)data;
    (void)steps;

    fmt_format(line, 34, "BOOT %02X%02X%02X%02X CLK %u", result->boot[0], result->boot[1], result->boot[2],
        result->boot[3], clock_khz(result->total_b[0], 2304u * 256u));
    *color = result->busy_timeouts != 0 || result->flag_timeouts != 0 ? COLOR_YELLOW : COLOR_WHITE;
}
