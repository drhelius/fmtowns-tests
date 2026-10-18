#include "tests.h"

// 1.5 RTC reference pulses (docs/12, HT-23): the MSM58321's address Eh outputs 1024 Hz on D0 and 122 us low pulses on
// D1 (seconds), D2 (minutes) and D3 (hours); READY is 0070h bit 7. Sets the clock and puts the time back at the end

#define RTC_DATA 0x0070
#define RTC_COMMAND 0x0080
#define RTC_SELECT 0x80
#define RTC_ADDRESS 0x81
#define RTC_WRITE 0x82
#define RTC_READ 0x84
#define REGISTER_DIVIDER 0x0D
#define REGISTER_REFERENCE 0x0E
#define LOG_SECONDS 40
#define LONG_EDGES 240
#define SHORT_EDGES 48
#define WINDOW_US 2000

#define SIGNAL_D0 0
#define SIGNAL_D1 1
#define SIGNAL_D2 2
#define SIGNAL_D3 3
#define SIGNAL_READY 7

typedef struct
{
    uint32_t time;
    uint8_t signal;
    uint8_t level;
    uint8_t reserved[2];
} edge_t;

typedef struct
{
    uint8_t saved[13];
    uint8_t written[13];
    uint8_t read_back[13];
    uint8_t reserved;
    uint32_t elapsed_us;
    uint16_t long_count;
    uint16_t rollover_count;
    uint16_t pulse_count;
    uint16_t reset_count;
    edge_t long_log[LONG_EDGES];
    edge_t rollover[SHORT_EDGES];
    edge_t pulse[SHORT_EDGES];
    edge_t reset[SHORT_EDGES];
} rtc_t;

static const test_step_t k_steps[] =
{
    { "SECONDS AT 30, 40 S LOG", TEST_CONFIRM },
    { "ROLLOVER AT XX:59:58", TEST_CONFIRM },
    { "READY AND D0 AROUND D1", TEST_CONFIRM },
    { "DIVIDER RESET", TEST_CONFIRM },
    { 0, 0 }
};

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static uint8_t rtc_read(uint8_t reg);
static void rtc_write(uint8_t reg, uint8_t value);
static void select_reference(void);
static uint16_t log_edges(edge_t* log, uint16_t capacity, uint32_t duration, uint8_t mask);
static uint16_t log_window(edge_t* log);
static void advance(uint8_t* time, uint32_t seconds);
static uint32_t first_fall(const edge_t* log, uint16_t count, uint8_t signal, uint32_t after);
static uint32_t pulse_width(const edge_t* log, uint16_t count, uint8_t signal);

const test_t g_test_rtc =
{
    "1.5", "RTC PULSES", "RTC!", TEST_CONFIRM, 1, sizeof(rtc_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    rtc_t* result = (rtc_t*)data;
    uint32_t start = tb_now();

    shell_step("READ THE TIME");

    for (uint8_t reg = 0; reg < 13; reg++)
        result->saved[reg] = rtc_read(reg) & 0x0F;

    if ((steps & 0x01) != 0)
    {
        shell_step("SECONDS 30, LOG %u S", LOG_SECONDS);
        rtc_write(1, 3);
        rtc_write(0, 0);
        select_reference();
        result->long_count = log_edges(result->long_log, LONG_EDGES, LOG_SECONDS * 1000000u, 0x8E);
    }

    if ((steps & 0x02) != 0)
    {
        shell_step("SET XX:59:58");
        rtc_write(3, 5);
        rtc_write(2, 9);
        rtc_write(1, 5);
        rtc_write(0, 8);
        select_reference();
        result->rollover_count = log_edges(result->rollover, SHORT_EDGES, 3000000u, 0x8E);
    }

    if ((steps & 0x04) != 0)
    {
        shell_step("READY AND D0 AROUND D1");
        select_reference();
        result->pulse_count = log_window(result->pulse);
    }

    if ((steps & 0x08) != 0)
    {
        shell_step("DIVIDER RESET");
        select_reference();
        uint32_t wait = tb_now();

        while ((in8(RTC_DATA) & 0x01) != 0 && tb_now() - wait < 2000)
            ;

        while ((in8(RTC_DATA) & 0x01) == 0 && tb_now() - wait < 2000)
            ;

        rtc_write(REGISTER_DIVIDER, 0x00);
        select_reference();
        result->reset_count = log_window(result->reset);
    }

    shell_step("PUT THE TIME BACK");
    result->elapsed_us = tb_now() - start;

    for (int reg = 0; reg < 13; reg++)
        result->written[reg] = result->saved[reg];

    advance(result->written, (result->elapsed_us + 500000) / 1000000);

    for (uint8_t reg = 0; reg < 13; reg++)
        rtc_write(reg, result->written[reg]);

    for (uint8_t reg = 0; reg < 13; reg++)
        result->read_back[reg] = rtc_read(reg) & 0x0F;

    out8(RTC_COMMAND, 0x00);
}

static uint8_t rtc_read(uint8_t reg)
{
    out8(RTC_COMMAND, RTC_SELECT);
    out8(RTC_DATA, reg);
    out8(RTC_COMMAND, RTC_ADDRESS);
    out8(RTC_COMMAND, RTC_SELECT);
    out8(RTC_COMMAND, RTC_READ);
    uint8_t value = in8(RTC_DATA);
    out8(RTC_COMMAND, RTC_SELECT);
    return value;
}

static void rtc_write(uint8_t reg, uint8_t value)
{
    out8(RTC_COMMAND, RTC_SELECT);
    out8(RTC_DATA, reg);
    out8(RTC_COMMAND, RTC_ADDRESS);
    out8(RTC_COMMAND, RTC_SELECT);
    out8(RTC_DATA, value);
    out8(RTC_COMMAND, RTC_WRITE);
    out8(RTC_COMMAND, RTC_SELECT);
}

// Address Eh latched with READ held, so every 0070h read returns READY and D3-D0
static void select_reference(void)
{
    out8(RTC_COMMAND, RTC_SELECT);
    out8(RTC_DATA, REGISTER_REFERENCE);
    out8(RTC_COMMAND, RTC_ADDRESS);
    out8(RTC_COMMAND, RTC_SELECT);
    out8(RTC_COMMAND, RTC_READ);
}

static uint16_t log_edges(edge_t* log, uint16_t capacity, uint32_t duration, uint8_t mask)
{
    uint32_t start = tb_now();
    uint8_t previous = in8(RTC_DATA) & mask;
    uint16_t count = 0;
    uint32_t shown = 0;

    for (;;)
    {
        uint32_t now = tb_now();
        uint8_t value = in8(RTC_DATA) & mask;
        uint8_t changed = value ^ previous;

        if (now - start >= duration)
            break;

        if (now - start - shown >= 1000000)
        {
            shown = now - start;
            shell_step("LOG %u S, %u EDGES", shown / 1000000, count);
        }

        for (int bit = 0; changed != 0 && bit < 8; bit++)
        {
            if ((changed & (1 << bit)) != 0 && count < capacity)
            {
                log[count].time = now - start;
                log[count].signal = (uint8_t)bit;
                log[count].level = (uint8_t)((value >> bit) & 1);
                count++;
            }
        }

        previous = value;
    }

    return count;
}

// Every edge of READY, D0 and D1 over 2 ms from the fall of READY before a counter update
static uint16_t log_window(edge_t* log)
{
    uint32_t wait = tb_now();

    while ((in8(RTC_DATA) & 0x80) == 0 && tb_now() - wait < 1100000)
        ;

    while ((in8(RTC_DATA) & 0x80) != 0 && tb_now() - wait < 1100000)
        ;

    return log_edges(log, SHORT_EDGES, WINDOW_US, 0x83);
}

// Seconds carry into minutes and hours; the date is left alone
static void advance(uint8_t* time, uint32_t seconds)
{
    uint32_t total = (time[1] * 10u + time[0]) + seconds;
    uint32_t minutes = (time[3] * 10u + time[2]) + total / 60;
    uint32_t hours = (time[5] & 0x03) * 10u + time[4] + minutes / 60;

    time[0] = (uint8_t)(total % 60 % 10);
    time[1] = (uint8_t)(total % 60 / 10);
    time[2] = (uint8_t)(minutes % 60 % 10);
    time[3] = (uint8_t)(minutes % 60 / 10);

    if ((time[5] & 0x08) != 0)
        hours %= 24;

    time[4] = (uint8_t)(hours % 10);
    time[5] = (uint8_t)((time[5] & 0x0C) | (hours / 10));
}

static uint32_t first_fall(const edge_t* log, uint16_t count, uint8_t signal, uint32_t after)
{
    for (int i = 0; i < count; i++)
    {
        if (log[i].signal == signal && log[i].level == 0 && log[i].time >= after)
            return log[i].time;
    }

    return 0xFFFFFFFFu;
}

static uint32_t pulse_width(const edge_t* log, uint16_t count, uint8_t signal)
{
    for (int i = 0; i < count; i++)
    {
        if (log[i].signal != signal || log[i].level != 0)
            continue;

        for (int j = i + 1; j < count; j++)
        {
            if (log[j].signal == signal && log[j].level == 1)
                return log[j].time - log[i].time;
        }
    }

    return 0;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 3;
}

static void page(const void* data, uint32_t steps, int number)
{
    const rtc_t* result = (const rtc_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        int counts[8] = { 0 };

        for (int i = 0; i < result->long_count; i++)
            counts[result->long_log[i].signal] += result->long_log[i].level == 0;

        uint32_t d1 = first_fall(result->long_log, result->long_count, SIGNAL_D1, 0);
        uint32_t ready = first_fall(result->long_log, result->long_count, SIGNAL_READY, 0);
        uint32_t d2 = first_fall(result->long_log, result->long_count, SIGNAL_D2, 0);

        shell_line(row++, COLOR_CYAN, "40 S FROM SECONDS = 30, %u EDGES", result->long_count);
        shell_line(row++, COLOR_WHITE, "FALLS D1 %u D2 %u D3 %u READY %u", counts[SIGNAL_D1], counts[SIGNAL_D2],
            counts[SIGNAL_D3], counts[SIGNAL_READY]);
        shell_line(row++, COLOR_WHITE, "D1 WIDTH %u US (EXPECTED 122)", pulse_width(result->long_log, result->long_count,
            SIGNAL_D1));
        shell_line(row++, COLOR_WHITE, "D1 AFTER READY FALLS %d US (244)", d1 != 0xFFFFFFFFu && ready != 0xFFFFFFFFu ?
            (int)(d1 - ready) : 0);

        if (d2 != 0xFFFFFFFFu)
            shell_line(row++, COLOR_WHITE, "D2 AT +%u.%u S (30.0 = COUNTERS)", d2 / 1000000, d2 / 100000 % 10);
        else
            shell_line(row++, COLOR_YELLOW, "NO D2 PULSE IN 40 S");

        row++;
        shell_line(row++, COLOR_CYAN, "FIRST EDGES: US SIGNAL LEVEL");

        for (int i = 0; i < result->long_count && row <= SHELL_BODY_BOTTOM; i++)
            shell_line(row++, COLOR_WHITE, "%10u %c %u", result->long_log[i].time,
                result->long_log[i].signal == SIGNAL_READY ? 'R' : (char)('0' + result->long_log[i].signal),
                result->long_log[i].level);

        return;
    }

    const edge_t* log = number == 1 ? result->rollover : result->pulse;
    uint16_t count = number == 1 ? result->rollover_count : result->pulse_count;

    if (number == 1)
    {
        uint32_t d2 = first_fall(log, count, SIGNAL_D2, 0);
        uint32_t d3 = first_fall(log, count, SIGNAL_D3, 0);
        shell_line(row++, COLOR_CYAN, "XX:59:58, 3 S, %u EDGES", count);
        shell_line(row++, COLOR_WHITE, "D3 WITH D2 %s", d2 != 0xFFFFFFFFu && d3 != 0xFFFFFFFFu &&
            (d3 > d2 ? d3 - d2 : d2 - d3) < 1000 ? "YES" : "NO");
    }
    else
        shell_line(row++, COLOR_CYAN, "2 MS FROM READY FALLING, %u EDGES", count);

    for (int i = 0; i < count && row <= SHELL_BODY_BOTTOM - (number == 2 ? 6 : 0); i++)
        shell_line(row++, COLOR_WHITE, "%10u %c %u", log[i].time, log[i].signal == SIGNAL_READY ? 'R' :
            (char)('0' + log[i].signal), log[i].level);

    if (number == 2)
    {
        uint32_t d0 = first_fall(result->pulse, result->pulse_count, SIGNAL_D0, 0);
        uint32_t d0_reset = first_fall(result->reset, result->reset_count, SIGNAL_D0, 0);
        row++;
        shell_line(row++, COLOR_WHITE, "D0 FIRST FALL %u US, AFTER RESET %u", d0, d0_reset);
        shell_line(row++, COLOR_WHITE, "RESET MOVES D0 %s", d0 % 977 != d0_reset % 977 ? "YES" : "NO");
        shell_line(row++, COLOR_WHITE, "TIME %X%X:%X%X:%X%X BACK %X%X:%X%X:%X%X", result->saved[5] & 3,
            result->saved[4], result->saved[3], result->saved[2], result->saved[1], result->saved[0],
            result->read_back[5] & 3, result->read_back[4], result->read_back[3], result->read_back[2],
            result->read_back[1], result->read_back[0]);
        shell_line(row++, COLOR_WHITE, "ELAPSED %u MS", result->elapsed_us / 1000);
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const rtc_t* result = (const rtc_t*)data;
    (void)steps;
    fmt_format(line, 34, "EDGES %u D1 WIDTH %u", result->long_count,
        pulse_width(result->long_log, result->long_count, SIGNAL_D1));
    *color = COLOR_WHITE;
}
