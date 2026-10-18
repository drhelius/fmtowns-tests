#include "tests.h"
#include "pic.h"

// 1.4 Interval Timer II (docs/12, HT-3): 0068h status and control, 006Ah/006Bh period in microseconds
// 0060h stays at its baseline, so the PIT can't raise IRQ0 meanwhile

#define TIMER2_STATUS 0x0068
#define TIMER2_LOW 0x006A
#define TIMER2_HIGH 0x006B
#define FLAG_TIMEOUT 0x40
#define FLAG_OVERRUN 0x20
#define DISABLE 0x80
#define SCAN_TIME 100000
#define STORM_LIMIT 2000
#define REPEATS 8
#define POLL_LIMIT 200000

#define START_006B 0
#define START_006A 1
#define START_ENABLE 2
#define START_NEW_PERIOD 3

typedef struct
{
    uint16_t minimum;
    uint16_t maximum;
} span_t;

typedef struct
{
    uint8_t present;
    uint8_t first_status;
    uint8_t level;
    uint8_t timeouts;
    uint16_t enabled[16];
    uint16_t disabled[16];
    uint8_t hold_seen;
    uint8_t hold_held;
    uint8_t hold_status;
    uint8_t hold_after_read;
    uint8_t gate_status;
    uint8_t stale_irq;
    uint16_t stale_us;
    uint8_t pending_before;
    uint8_t pending_after;
    uint8_t status_count;
    uint8_t reserved;
    span_t starts[4];
    span_t first_1000;
    uint32_t ten_periods;
    uint32_t period_zero;
    uint16_t readback[16];
    uint8_t statuses[8];
} timer2_t;

static volatile uint32_t s_counts[16];
static volatile uint32_t s_first_irq;
static volatile int s_level;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void handler(int irq);
static void timed_handler(int irq);
static void program(uint16_t period);
static uint8_t read_status(timer2_t* result);
static uint32_t wait_flag(timer2_t* result, uint32_t limit);
static int irr_bit(int level);
static void scan(timer2_t* result, uint16_t* counts, uint8_t control);
static void hold(timer2_t* result);
static void gate(timer2_t* result);
static void pending(timer2_t* result);
static void starts(timer2_t* result);
static void accuracy(timer2_t* result);
static void readback(timer2_t* result);
static void span_add(span_t* span, uint32_t value, int first);
static int verdict_gate(const timer2_t* result);
static int verdict_start(const timer2_t* result);
static int verdict_reload(const timer2_t* result);
static int verdict_readback(const timer2_t* result);

const test_t g_test_timer2 =
{
    "1.4", "INTERVAL TIMER II", 0, TEST_RUN_ALL, 1, sizeof(timer2_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    timer2_t* result = (timer2_t*)data;
    (void)steps;

    result->first_status = in8(TIMER2_STATUS);
    result->present = result->first_status != 0xFF;

    if (!result->present)
        return;

    result->statuses[result->status_count++] = result->first_status;

    shell_step("LEVEL SCAN ENABLED");
    scan(result, result->enabled, 0x00);
    shell_step("LEVEL SCAN DISABLED");
    scan(result, result->disabled, DISABLE);

    result->level = 0xFF;

    for (int level = 0, best = 0; level < 16; level++)
    {
        if (result->enabled[level] > best && result->disabled[level] == 0 && result->enabled[level] < STORM_LIMIT)
        {
            best = result->enabled[level];
            result->level = (uint8_t)level;
        }
    }

    shell_step("HOLD");
    hold(result);
    shell_step("MASK OR GATE");
    gate(result);
    shell_step("DISABLE WHILE PENDING");
    pending(result);
    shell_step("START AND RELOAD");
    starts(result);
    shell_step("ACCURACY");
    accuracy(result);
    shell_step("READBACK");
    readback(result);
    out8(TIMER2_STATUS, DISABLE);

    if (result->timeouts != 0)
        shell_set_status(RECORD_STATUS_TIMEOUT);
}

static void handler(int irq)
{
    s_counts[irq]++;
    (void)in8(TIMER2_STATUS);

    if (s_counts[irq] >= STORM_LIMIT)
        pic_mask(irq);

    pic_eoi(irq);
}

static void timed_handler(int irq)
{
    if (s_first_irq == 0)
        s_first_irq = tb_now() | 1;

    (void)in8(TIMER2_STATUS);
    pic_mask(irq);
    pic_eoi(irq);
}

static void program(uint16_t period)
{
    out8(TIMER2_LOW, (uint8_t)period);
    out8(TIMER2_HIGH, (uint8_t)(period >> 8));
}

static uint8_t read_status(timer2_t* result)
{
    uint8_t value = in8(TIMER2_STATUS);
    int known = 0;

    for (int i = 0; i < result->status_count; i++)
        known |= result->statuses[i] == value;

    if (!known && result->status_count < sizeof(result->statuses))
        result->statuses[result->status_count++] = value;

    return value;
}

// Polls the status until a timeout flag shows, and returns the time it took, or zero
static uint32_t wait_flag(timer2_t* result, uint32_t limit)
{
    uint32_t start = tb_now();

    for (;;)
    {
        uint32_t now = tb_now();

        if ((read_status(result) & FLAG_TIMEOUT) != 0)
            return (now - start) | 1;

        if (now - start > limit)
        {
            result->timeouts++;
            return 0;
        }
    }
}

static int irr_bit(int level)
{
    return (pic_read_irr() >> level) & 1;
}

// One level at a time for 100 ms with interrupts on; a level that storms is masked after 2,000 requests
static void scan(timer2_t* result, uint16_t* counts, uint8_t control)
{
    for (int level = 0; level < 16; level++)
    {
        s_counts[level] = 0;
        cpu_set_irq_handler(level, handler);
    }

    for (int level = 0; level < 16; level++)
    {
        pic_mask_all();
        out8(TIMER2_STATUS, DISABLE);
        (void)read_status(result);
        program(1000);
        out8(TIMER2_STATUS, control);
        pic_unmask(level);
        sti();
        uint32_t start = tb_now();

        while (tb_now() - start < SCAN_TIME)
            ;

        cli();
        pic_mask_all();
        out8(TIMER2_STATUS, DISABLE);
        counts[level] = (uint16_t)(s_counts[level] > 0xFFFF ? 0xFFFF : s_counts[level]);
        (void)read_status(result);
        pic_init(g_machine.id);
    }
}

// With the level unmasked and IF = 0, the request in IRR before and after reading 0068h
static void hold(timer2_t* result)
{
    int level = result->level < 16 ? result->level : 0;

    pic_mask_all();
    out8(TIMER2_STATUS, DISABLE);
    (void)read_status(result);
    program(2000);
    out8(TIMER2_STATUS, 0x00);
    pic_unmask(level);
    uint32_t start = tb_now();

    while (!irr_bit(level) && tb_now() - start < 10000)
        ;

    result->hold_seen = (uint8_t)irr_bit(level);
    result->hold_held = 1;
    start = tb_now();

    while (tb_now() - start < 5000)
    {
        if (!irr_bit(level))
            result->hold_held = 0;
    }

    result->hold_status = read_status(result);
    result->hold_after_read = (uint8_t)irr_bit(level);
    out8(TIMER2_STATUS, DISABLE);
    pic_mask_all();
}

// Counting while disabled, then whether enabling raises the stale request at once
static void gate(timer2_t* result)
{
    int level = result->level < 16 ? result->level : 0;

    out8(TIMER2_STATUS, DISABLE);
    (void)read_status(result);
    program(1000);
    tb_delay(5000);
    result->gate_status = read_status(result);

    out8(TIMER2_STATUS, DISABLE);
    (void)read_status(result);
    program(1000);
    tb_delay(5000);
    s_first_irq = 0;
    cpu_set_irq_handler(level, timed_handler);
    pic_mask_all();
    pic_unmask(level);
    sti();
    uint32_t start = tb_now();
    out8(TIMER2_STATUS, 0x00);

    while (s_first_irq == 0 && tb_now() - start < 10000)
        ;

    cli();
    pic_mask_all();
    out8(TIMER2_STATUS, DISABLE);
    result->stale_irq = s_first_irq != 0;
    result->stale_us = s_first_irq != 0 ? (uint16_t)(s_first_irq - start) : 0xFFFF;
    pic_init(g_machine.id);
}

static void pending(timer2_t* result)
{
    int level = result->level < 16 ? result->level : 0;

    pic_mask_all();
    out8(TIMER2_STATUS, DISABLE);
    (void)read_status(result);
    program(1000);
    out8(TIMER2_STATUS, 0x00);
    pic_unmask(level);
    uint32_t start = tb_now();

    while (!irr_bit(level) && tb_now() - start < 10000)
        ;

    result->pending_before = (uint8_t)irr_bit(level);
    out8(TIMER2_STATUS, DISABLE);
    result->pending_after = (uint8_t)irr_bit(level);
    (void)read_status(result);
    pic_mask_all();
}

// The time from a write to the first timeout flag, with the PIC masked so no request is taken
static void starts(timer2_t* result)
{
    pic_mask_all();

    for (int repeat = 0; repeat < REPEATS; repeat++)
    {
        out8(TIMER2_STATUS, 0x00);
        out8(TIMER2_LOW, 0xE8);
        (void)read_status(result);
        out8(TIMER2_HIGH, 0x03);
        span_add(&result->starts[START_006B], wait_flag(result, POLL_LIMIT), repeat == 0);

        tb_delay(300 + repeat * 70);
        (void)read_status(result);
        out8(TIMER2_LOW, 0xE8);
        span_add(&result->starts[START_006A], wait_flag(result, POLL_LIMIT), repeat == 0);

        out8(TIMER2_STATUS, DISABLE);
        program(1000);
        tb_delay(3000 + repeat * 70);
        (void)read_status(result);
        out8(TIMER2_STATUS, 0x00);
        span_add(&result->starts[START_ENABLE], wait_flag(result, POLL_LIMIT), repeat == 0);

        program(2000);
        wait_flag(result, POLL_LIMIT);
        uint32_t start = tb_now();
        tb_delay(500);
        program(4000);
        uint32_t flag = wait_flag(result, POLL_LIMIT);
        span_add(&result->starts[START_NEW_PERIOD], flag != 0 ? tb_now() - start : 0, repeat == 0);
    }

    out8(TIMER2_STATUS, DISABLE);
}

static void accuracy(timer2_t* result)
{
    pic_mask_all();
    out8(TIMER2_STATUS, 0x00);

    for (int repeat = 0; repeat < REPEATS; repeat++)
    {
        out8(TIMER2_LOW, 0xE8);
        (void)read_status(result);
        out8(TIMER2_HIGH, 0x03);
        span_add(&result->first_1000, wait_flag(result, POLL_LIMIT), repeat == 0);
    }

    (void)read_status(result);
    program(1000);
    wait_flag(result, POLL_LIMIT);
    uint32_t start = tb_now();
    int counted = 0;

    for (int i = 0; i < 10; i++)
        counted += wait_flag(result, POLL_LIMIT) != 0;

    result->ten_periods = counted == 10 ? tb_now() - start : 0;
    out8(TIMER2_LOW, 0x00);
    (void)read_status(result);
    out8(TIMER2_HIGH, 0x00);
    result->period_zero = wait_flag(result, 100000);
    out8(TIMER2_STATUS, DISABLE);
}

static void readback(timer2_t* result)
{
    out8(TIMER2_STATUS, 0x00);
    program(1000);

    for (int i = 0; i < 16; i++)
    {
        result->readback[i] = (uint16_t)(in8(TIMER2_LOW) | (in8(TIMER2_HIGH) << 8));
        tb_delay(70);
    }

    tb_delay(2500);
    (void)read_status(result);
    out8(TIMER2_STATUS, DISABLE);
    (void)read_status(result);
}

static void span_add(span_t* span, uint32_t value, int first)
{
    uint16_t clipped = (uint16_t)(value > 0xFFFF ? 0xFFFF : value);

    if (first || clipped < span->minimum)
        span->minimum = clipped;

    if (first || clipped > span->maximum)
        span->maximum = clipped;
}

// 0 counts while disabled (enable is a mask), 1 the count stops
static int verdict_gate(const timer2_t* result)
{
    return (result->gate_status & (FLAG_TIMEOUT | FLAG_OVERRUN)) != 0 ? 0 : 1;
}

// Which write starts an interval: a full period after the 006Bh write, after the 006Ah write, or after enabling
static int verdict_start(const timer2_t* result)
{
    int after_006b = result->starts[START_006B].minimum >= 950 && result->starts[START_006B].maximum <= 1100;
    int after_006a = result->starts[START_006A].minimum >= 950 && result->starts[START_006A].maximum <= 1100;
    int after_enable = result->starts[START_ENABLE].minimum >= 950 && result->starts[START_ENABLE].maximum <= 1100;

    if (after_006b && !after_006a && !after_enable)
        return 0;

    if (after_006a)
        return 1;

    if (after_enable)
        return 2;

    return 3;
}

// A new period written 500 us into a 2,000 us interval: 4,500 us when it restarts, 1,500 when it waits
static int verdict_reload(const timer2_t* result)
{
    return result->starts[START_NEW_PERIOD].minimum > 3000 ? 0 : 1;
}

static int verdict_readback(const timer2_t* result)
{
    for (int i = 1; i < 16; i++)
    {
        if (result->readback[i] != result->readback[0])
            return 1;
    }

    return 0;
}

static int pages(const void* data, uint32_t steps)
{
    const timer2_t* result = (const timer2_t*)data;
    (void)steps;
    return result->present ? 3 : 1;
}

static void page(const void* data, uint32_t steps, int number)
{
    static const char* const k_gate[] = { "EN MASKS ONLY", "EN GATES COUNT" };
    static const char* const k_start[] = { "STARTS AT 006BH", "STARTS AT 006AH", "STARTS AT ENABLE", "FREE RUNNING" };
    static const char* const k_reload[] = { "NEW PERIOD RESTARTS", "NEW PERIOD WAITS" };
    static const char* const k_readback[] = { "READS PERIOD", "READS COUNT" };
    const timer2_t* result = (const timer2_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (!result->present)
    {
        shell_line(row++, COLOR_YELLOW, "0068H READS FF: NOT PRESENT");
        shell_line(row++, COLOR_WHITE, "HT-3 NEEDS A 10F/20F OR LATER MODEL");
        return;
    }

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "IRQS IN 100 MS, 1000 US PERIOD");
        shell_line(row++, COLOR_GRAY, "LVL ENABLED DISABLED  LVL EN   DIS");

        for (int level = 0; level < 8; level++)
            shell_line(row++, level == result->level || level + 8 == result->level ? COLOR_YELLOW : COLOR_WHITE,
                "%2u  %5u   %5u     %2u  %5u %5u", level, result->enabled[level], result->disabled[level], level + 8,
                result->enabled[level + 8], result->disabled[level + 8]);

        row++;

        if (result->level < 16)
            shell_line(row++, COLOR_WHITE, "TIMER II ON IRQ%u", result->level);
        else
            shell_line(row++, COLOR_YELLOW, "NO LEVEL COUNTS ONLY WHEN ENABLED");

        shell_line(row++, COLOR_GRAY, "%u+ IS A STORM, MASKED", STORM_LIMIT);
        shell_line(row++, COLOR_WHITE, "FIRST 0068H READ %02X", result->first_status);
        return;
    }

    if (number == 1)
    {
        int gate = verdict_gate(result);
        int start = verdict_start(result);
        int reload = verdict_reload(result);
        int back = verdict_readback(result);

        shell_line(row++, COLOR_CYAN, "BEHAVIOR");
        shell_line(row++, COLOR_WHITE, "%s", k_gate[gate]);
        shell_line(row++, COLOR_WHITE, " 0068H AFTER 5 MS DISABLED %02X", result->gate_status);
        shell_line(row++, COLOR_WHITE, "%s", result->stale_irq ? "STALE IRQ ON ENABLE" : "NO STALE IRQ ON ENABLE");
        shell_line(row++, COLOR_WHITE, " FIRST IRQ %u US AFTER ENABLE", result->stale_us);
        shell_line(row++, COLOR_WHITE, "%s", result->pending_after ? "DISABLE KEEPS PENDING" : "DISABLE DROPS PENDING");
        shell_line(row++, COLOR_WHITE, " IRR BEFORE %u AFTER %u", result->pending_before, result->pending_after);
        shell_line(row++, COLOR_WHITE, "%s", k_start[start]);
        shell_line(row++, COLOR_WHITE, "%s", k_reload[reload]);
        shell_line(row++, COLOR_WHITE, "%s", k_readback[back]);
        shell_line(row++, COLOR_WHITE, "HELD UNTIL READ %s, IRR AFTER READ %u", result->hold_held ? "YES" : "NO",
            result->hold_after_read);
        shell_line(row++, COLOR_WHITE, " SEEN %u STATUS %02X", result->hold_seen, result->hold_status);

        char seen[32];
        int length = 0;

        for (int i = 0; i < result->status_count; i++)
            length += fmt_format(seen + length, sizeof(seen) - length, "%02X ", result->statuses[i]);

        seen[length] = 0;
        shell_line(row++, COLOR_WHITE, "0068H VALUES %s", seen);
        return;
    }

    static const char* const k_names[] = { "AFTER 006BH", "AFTER 006AH", "AFTER ENABLE", "NEW PERIOD" };

    shell_line(row++, COLOR_CYAN, "TIMES IN US, MIN-MAX OF %u", REPEATS);

    for (int i = 0; i < 4; i++)
        shell_line(row++, COLOR_WHITE, "%-12s %5u-%5u", k_names[i], result->starts[i].minimum,
            result->starts[i].maximum);

    shell_line(row++, COLOR_WHITE, "FIRST 1000   %5u-%5u", result->first_1000.minimum, result->first_1000.maximum);
    shell_line(row++, COLOR_WHITE, "TEN PERIODS  %u", result->ten_periods);
    shell_line(row++, COLOR_WHITE, "PERIOD 0     %u", result->period_zero);
    shell_line(row++, COLOR_GRAY, "PROBE: 1022, 9990, 65550");
    row++;
    shell_line(row++, COLOR_CYAN, "006AH/006BH READ ACROSS AN INTERVAL");

    for (int i = 0; i < 16; i += 4)
        shell_line(row++, COLOR_WHITE, "%04X %04X %04X %04X", result->readback[i], result->readback[i + 1],
            result->readback[i + 2], result->readback[i + 3]);

    if (result->timeouts != 0)
        shell_line(row++, COLOR_YELLOW, "%u WAITS TIMED OUT", result->timeouts);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const timer2_t* result = (const timer2_t*)data;
    (void)steps;

    if (!result->present)
    {
        fmt_format(line, 34, "NOT PRESENT");
        *color = COLOR_GRAY;
        return;
    }

    fmt_format(line, 34, "IRQ%u GATE %u START %u 1ST %u", result->level, verdict_gate(result), verdict_start(result),
        result->first_1000.minimum);
    *color = result->timeouts != 0 ? COLOR_YELLOW : COLOR_WHITE;
}
