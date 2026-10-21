#include "tests.h"

// 4.2 Mouse strobe timing (docs/12, HT-2): the Towns mouse in port 2, its COM (04D6h bit 5) toggled four times a
// packet from high, each edge timed by the time base with interrupts off. Each nibble is read at the Data Book's
// sample point (90 us after the first edge, 45 after the others) and again just before the next edge. Packets are
// 16 ms apart, as a game polls once a frame, so steady motion shows in each. One interval swept at a time while the
// mouse moves down: X reads 0 and Y's high nibble F, so a packet that restarts at the swept edge shows X's 0 where
// Y's nibble belongs, whether the restart takes a new sample or not. The edges come from a tight loop on the time
// base; the gaps logged are the ones measured

#define VALUES 7
#define REPEATS 6
#define T4_VALUES 5
#define CLEAR_CASES 3
#define CLEAR_REPEATS 2
#define CLEAR_PACKETS 3
#define LIMIT_RUNS 2
#define LIMIT_PACKETS 24
#define FRAME_US 16000u
#define DEFAULT_US 100u
#define TIMEOUT_US 1000u
#define COM_LOW 0x0F
#define COM_HIGH 0x2F

typedef struct
{
    uint8_t early[4];
    uint8_t late[4];
    uint16_t gaps[4];
} packet_t;

typedef struct
{
    uint8_t ran;
    uint8_t limit_counts[LIMIT_RUNS];
    uint8_t reserved;
    packet_t sweep_t2[VALUES][REPEATS];
    packet_t sweep_t1[VALUES][REPEATS];
    packet_t sweep_t3[VALUES][REPEATS];
    packet_t sweep_t4[T4_VALUES][REPEATS];
    packet_t clear[CLEAR_CASES][CLEAR_REPEATS][CLEAR_PACKETS];
    packet_t limits[LIMIT_RUNS][LIMIT_PACKETS];
} mouse_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void idle(void);
static void packet(packet_t* entry, uint32_t t4, uint32_t t1, uint32_t t2, uint32_t t3);
static void flush(void);
static int moving(const char* what, uint32_t seconds);
static void step_t2(mouse_t* result);
static void step_others(mouse_t* result);
static void step_clear(mouse_t* result);
static void step_limits(mouse_t* result);
static int restarted(const packet_t* entry, const packet_t* control, int index);
static int verdict(const packet_t entries[][REPEATS], int value, int index);
static uint32_t timeout_from(const mouse_t* result);
static int draw_sweep(int row, const char* name, const uint16_t* values, int count, const packet_t entries[][REPEATS],
    int index);
static void nibbles(const packet_t* entry, char* text);

static const test_step_t k_steps[] =
{
    { "T2 SWEEP, MOVING ALONG X", TEST_PHOTO },
    { "T1, T3 AND T4 SWEEPS", TEST_PHOTO },
    { "WHAT A TIMEOUT CLEARS", TEST_PHOTO },
    { "8-BIT LIMITS", TEST_PHOTO },
    { 0, 0 }
};

static const uint16_t k_values[VALUES] = { 100, 140, 160, 200, 300, 500, 1000 };
static const uint16_t k_t4_values[T4_VALUES] = { 50, 100, 200, 300, 1000 };
static const char* const k_clear_names[CLEAR_CASES] = { "NO TIMEOUT", "TIMEOUT AT T2", "TIMEOUT AT T1" };

uint32_t mouse_packet(void* entry, const uint32_t* waits, uint32_t since);
uint32_t mouse_packet_pit(void* entry, const uint32_t* waits, uint32_t since);

static uint32_t s_last_edge;
static uint16_t s_last_count;

const test_t g_test_mouse =
{
    "4.2", "MOUSE STROBE TIMING", "MOUSE", 0, 1, sizeof(mouse_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    mouse_t* result = (mouse_t*)data;

    ui_pad_only(0);
    ui_prompt("THE TOWNS MOUSE IN PORT 2,", "THE PAD IN PORT 1");

    if (ui_wait(30, "A STARTS, B SKIPS") != UI_A)
        return;

    idle();

    if ((steps & 0x01) != 0)
        step_t2(result);

    if ((steps & 0x02) != 0)
        step_others(result);

    if ((steps & 0x04) != 0)
        step_clear(result);

    if ((steps & 0x08) != 0)
        step_limits(result);

    out8(0x04D6, COM_LOW);
    scr_clear_rows(SHELL_BODY_TOP, SHELL_BODY_BOTTOM);
}

// COM high with nothing going on, long enough for any packet in progress to time out
static void idle(void)
{
    out8(0x04D6, COM_HIGH);
    tb_delay(20000u);
    s_last_edge = tb_now();
    s_last_count = tb_source() == TB_SOURCE_0026 ? in16(0x0026) : tb_pit_read();
}

// T4 from the previous packet's last edge, then the four edges T1, T2 and T3 apart; COM ends high. The loop is in
// assembly, on 0026h or else on PIT counter 0 (3.26 us counts, the gaps turned back into microseconds)
static void packet(packet_t* entry, uint32_t t4, uint32_t t1, uint32_t t2, uint32_t t3)
{
    uint32_t waits[5] = { t4, t1, t2, t3, 90u };

    if (tb_source() == TB_SOURCE_0026)
        s_last_count = (uint16_t)mouse_packet(entry, waits, s_last_count);
    else
    {
        for (int i = 0; i < 5; i++)
            waits[i] = (waits[i] * 192u + 312u) / 625u;

        s_last_count = (uint16_t)mouse_packet_pit(entry, waits, s_last_count);

        for (int i = 0; i < 4; i++)
            entry->gaps[i] = (uint16_t)((uint32_t)entry->gaps[i] * 625u / 192u);
    }

    s_last_edge = tb_now();
}

// Eight normal packets 2 ms apart, so what built up before a sweep doesn't reach it
static void flush(void)
{
    for (int i = 0; i < 8; i++)
    {
        packet_t entry;
        packet(&entry, 2000u, DEFAULT_US, DEFAULT_US, DEFAULT_US);
    }
}

// A prompt to start moving, then the moving time; B skips the part
static int moving(const char* what, uint32_t seconds)
{
    ui_prompt(what, "A STARTS: MOVE AT ONCE AND KEEP ON");

    if (ui_wait(20, "A STARTS, B SKIPS") != UI_A)
        return 0;

    ui_prompt(what, "KEEP MOVING");
    ui_countdown(tb_now(), seconds * 1000000u, "MOVING");
    tb_delay(300000u);
    idle();
    return 1;
}

static void step_t2(mouse_t* result)
{
    shell_step("T2 SWEEP");

    if (!moving("MOVE THE MOUSE DOWN, STEADILY", 2))
        return;

    flush();

    for (int r = 0; r < REPEATS; r++)
    {
        for (int v = 0; v < VALUES; v++)
            packet(&result->sweep_t2[v][r], FRAME_US, DEFAULT_US, k_values[v], DEFAULT_US);
    }

    result->ran |= 0x01;
    idle();
}

static void step_others(mouse_t* result)
{
    shell_step("T1 AND T3 SWEEPS");

    if (!moving("MOVE THE MOUSE DOWN, STEADILY", 3))
        return;

    flush();

    for (int r = 0; r < REPEATS; r++)
    {
        for (int v = 0; v < VALUES; v++)
            packet(&result->sweep_t1[v][r], FRAME_US, k_values[v], DEFAULT_US, DEFAULT_US);
    }

    for (int r = 0; r < REPEATS; r++)
    {
        for (int v = 0; v < VALUES; v++)
            packet(&result->sweep_t3[v][r], FRAME_US, DEFAULT_US, DEFAULT_US, k_values[v]);
    }

    for (int r = 0; r < REPEATS; r++)
    {
        for (int v = 0; v < T4_VALUES; v++)
        {
            packet_t primer;
            packet(&primer, FRAME_US, DEFAULT_US, DEFAULT_US, DEFAULT_US);
            packet(&result->sweep_t4[v][r], k_t4_values[v], DEFAULT_US, DEFAULT_US, DEFAULT_US);
        }
    }

    result->ran |= 0x02;
    idle();
}

// Motion for a second with no strobes, a stop, then one packet of the case and two normal ones 2 ms apart
static void step_clear(mouse_t* result)
{
    shell_step("WHAT A TIMEOUT CLEARS");

    for (int c = 0; c < CLEAR_CASES; c++)
    {
        for (int r = 0; r < CLEAR_REPEATS; r++)
        {
            packet_t* entries = result->clear[c][r];
            char text[40];

            fmt_format(text, sizeof(text), "%s %u/%u: MOVE LEFT 1 S", k_clear_names[c], r + 1, CLEAR_REPEATS);

            if (!moving(text, 1))
                return;

            tb_delay(700000u);
            ui_prompt("STOP NOW, HANDS OFF", 0);
            tb_delay(500000u);
            idle();
            packet(&entries[0], FRAME_US, c == 2 ? TIMEOUT_US : DEFAULT_US, c == 1 ? TIMEOUT_US : DEFAULT_US,
                DEFAULT_US);
            packet(&entries[1], 2000u, DEFAULT_US, DEFAULT_US, DEFAULT_US);
            packet(&entries[2], 2000u, DEFAULT_US, DEFAULT_US, DEFAULT_US);
        }
    }

    result->ran |= 0x04;
    idle();
}

// Fast motion for a second with no strobes, then packets 2 ms apart until two read zero
static void step_limits(mouse_t* result)
{
    static const char* const k_moves[LIMIT_RUNS] = { "MOVE FAST RIGHT FOR 1 S", "MOVE FAST DOWN FOR 1 S" };

    shell_step("8-BIT LIMITS");

    for (int run = 0; run < LIMIT_RUNS; run++)
    {
        int zeros = 0;

        if (!moving(k_moves[run], 1))
            return;

        tb_delay(700000u);
        ui_prompt("STOP NOW, HANDS OFF", 0);
        tb_delay(300000u);
        idle();

        for (int i = 0; i < LIMIT_PACKETS && zeros < 2; i++)
        {
            packet_t* entry = &result->limits[run][i];
            packet(entry, 2000u, DEFAULT_US, DEFAULT_US, DEFAULT_US);
            result->limit_counts[run] = (uint8_t)(i + 1);
            zeros = (entry->early[0] & 0x0F) == 0 && (entry->early[1] & 0x0F) == 0 &&
                (entry->early[2] & 0x0F) == 0 && (entry->early[3] & 0x0F) == 0 ? zeros + 1 : 0;
        }
    }

    result->ran |= 0x08;
    idle();
}

// The nibble Y put there in the 100 us packet of the same round reads 0 here: X's, so the packet started again
static int restarted(const packet_t* entry, const packet_t* control, int index)
{
    return (control->early[index] & 0x0F) != 0 && (entry->early[index] & 0x0F) == 0;
}

// R when most rounds with motion restarted, C when most went on, ? with fewer than three rounds moving
static int verdict(const packet_t entries[][REPEATS], int value, int index)
{
    int moving_rounds = 0;
    int count = 0;

    for (int r = 0; r < REPEATS; r++)
    {
        moving_rounds += (entries[0][r].early[index] & 0x0F) != 0;
        count += restarted(&entries[value][r], &entries[0][r], index);
    }

    if (moving_rounds < 3)
        return '?';

    return count * 2 > moving_rounds ? 'R' : 'C';
}

// The shortest T2 from which most packets with motion restarted, 0 if none did
static uint32_t timeout_from(const mouse_t* result)
{
    for (int v = 1; v < VALUES; v++)
    {
        if (verdict(result->sweep_t2, v, 2) == 'R')
            return k_values[v];
    }

    return 0;
}

static void nibbles(const packet_t* entry, char* text)
{
    static const char k_hex[] = "0123456789ABCDEF";

    for (int i = 0; i < 4; i++)
        text[i] = k_hex[entry->early[i] & 0x0F];

    text[4] = 0;
}

// One row per value: the nibbles of the first five packets and the verdict over all six
static int draw_sweep(int row, const char* name, const uint16_t* values, int count, const packet_t entries[][REPEATS],
    int index)
{
    for (int v = 0; v < count; v++)
    {
        char text[5][5];

        for (int r = 0; r < 5; r++)
            nibbles(&entries[v][r], text[r]);

        shell_line(row++, COLOR_WHITE, "%s%5u %s %s %s %s %s %c", name, values[v], text[0], text[1], text[2], text[3],
            text[4], index >= 0 ? verdict(entries, v, index) : ' ');
    }

    return row;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 4;
}

static void page(const void* data, uint32_t steps, int number)
{
    const mouse_t* result = (const mouse_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "T2 US, NIBBLES XH XL YH YL");

        if ((result->ran & 0x01) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        row = draw_sweep(row, "T2", k_values, VALUES, result->sweep_t2, 2);
        row++;

        uint32_t from = timeout_from(result);

        if (from != 0)
            shell_line(row++, COLOR_GREEN, "TIMEOUT FROM %u US", from);
        else
            shell_line(row++, COLOR_GREEN, "NO TIMEOUT UP TO 1000 US");

        shell_line(row++, COLOR_WHITE, "GAPS, 160 US ROW: %u %u %u %u", result->sweep_t2[2][0].gaps[0],
            result->sweep_t2[2][0].gaps[1], result->sweep_t2[2][0].gaps[2], result->sweep_t2[2][0].gaps[3]);
        shell_line(row++, COLOR_WHITE, "LATE READS, 1000 US ROW: %02X %02X %02X %02X",
            result->sweep_t2[VALUES - 1][0].late[0], result->sweep_t2[VALUES - 1][0].late[1],
            result->sweep_t2[VALUES - 1][0].late[2], result->sweep_t2[VALUES - 1][0].late[3]);
        shell_line(row++, COLOR_GRAY, "MOVING DOWN: 00FX WHEN IT WENT ON");
        shell_line(row++, COLOR_GRAY, "R 3RD NIBBLE 0: RESTARTED, C WENT ON");
        return;
    }

    if (number == 1)
    {
        shell_line(row++, COLOR_CYAN, "T1, T3, T4 US, NIBBLES");

        if ((result->ran & 0x02) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        row = draw_sweep(row, "T1", k_values, VALUES, result->sweep_t1, 2);
        row = draw_sweep(row, "T3", k_values, VALUES, result->sweep_t3, 3);
        row = draw_sweep(row, "T4", k_t4_values, T4_VALUES, result->sweep_t4, -1);
        shell_line(row++, COLOR_GRAY, "R: 0 WHERE Y'S NIBBLE WAS (3RD, 4TH)");
        return;
    }

    if (number == 2)
    {
        shell_line(row++, COLOR_CYAN, "MOVE, STOP, CASE PACKET, 2 NORMAL");

        if ((result->ran & 0x04) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        for (int c = 0; c < CLEAR_CASES; c++)
        {
            shell_line(row++, COLOR_WHITE, "%s", k_clear_names[c]);

            for (int r = 0; r < CLEAR_REPEATS; r++)
            {
                const packet_t* entries = result->clear[c][r];
                shell_line(row++, COLOR_WHITE, "  %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
                    entries[0].early[0] & 0x0F, entries[0].early[1] & 0x0F, entries[0].early[2] & 0x0F,
                    entries[0].early[3] & 0x0F, entries[1].early[0] & 0x0F, entries[1].early[1] & 0x0F,
                    entries[1].early[2] & 0x0F, entries[1].early[3] & 0x0F, entries[2].early[0] & 0x0F,
                    entries[2].early[1] & 0x0F, entries[2].early[2] & 0x0F, entries[2].early[3] & 0x0F);
            }
        }

        row++;
        shell_line(row++, COLOR_GRAY, "MOTION IN THE 2ND PACKET AFTER A");
        shell_line(row++, COLOR_GRAY, "TIMEOUT: THE MOTION WAS KEPT");
        return;
    }

    shell_line(row++, COLOR_CYAN, "FAST MOTION, PACKETS 2 MS APART");

    if ((result->ran & 0x08) == 0)
    {
        shell_line(row++, COLOR_GRAY, "NOT RUN");
        return;
    }

    for (int run = 0; run < LIMIT_RUNS; run++)
    {
        char line[40];
        int length = 0;

        shell_line(row++, COLOR_WHITE, "%s, %u PACKETS, X Y", run == 0 ? "RIGHT" : "DOWN", result->limit_counts[run]);

        for (int i = 0; i < result->limit_counts[run]; i++)
        {
            const packet_t* entry = &result->limits[run][i];
            uint8_t x = (uint8_t)(((entry->early[0] & 0x0F) << 4) | (entry->early[1] & 0x0F));
            uint8_t y = (uint8_t)(((entry->early[2] & 0x0F) << 4) | (entry->early[3] & 0x0F));

            length += fmt_format(line + length, (int)sizeof(line) - length, "%02X%02X ", x, y);

            if (length >= 30 || i + 1 == result->limit_counts[run])
            {
                shell_line(row++, COLOR_WHITE, " %s", line);
                length = 0;
            }
        }
    }

    shell_line(row++, COLOR_GRAY, "FIRST 81 OR 7F: SATURATES; REST NEXT");
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const mouse_t* result = (const mouse_t*)data;
    (void)steps;

    *color = COLOR_WHITE;

    if ((result->ran & 0x01) == 0)
    {
        fmt_copy(line, result->ran != 0 ? "T2 SWEEP NOT RUN" : "NOT RUN", 34);
        return;
    }

    uint32_t from = timeout_from(result);

    if (from != 0)
        fmt_format(line, 34, "T2 TIMEOUT FROM %u US", from);
    else
        fmt_copy(line, "T2 NO TIMEOUT UP TO 1000 US", 34);
}
