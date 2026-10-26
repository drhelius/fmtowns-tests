#include "tests.h"

// 4.1 Pad ports (docs/12, HT-11): both ports read in four output states of 04D6h, COM low and high with the trigger
// outputs high, then the same with them low. Prompts of 3 s name a button to hold; the last 2 s are sampled, keeping
// the AND and the OR of each state, so a bit that moved shows. The bits that differ from NOTHING are the button's,
// and six reads right after COM rises show how soon bit 6 follows it. Each prompt names only the button to hold now,
// and the verdicts take NOTHING AGAIN as the idle state when it sampled, so a press during NOTHING can't fake a bit

#define PROMPTS 12
#define STATES 4
#define RISE_READS 6
#define PROMPT_US 3000000u
#define SETTLE_US 1000000u
#define CHANGES 24
#define LIVE_LIMIT_US 120000000u
#define LEAVE_US 1000000u
#define STATE_DELAY_US 20
#define OUTPUT_IDLE 0x0F
#define ZOOM 9
#define AGAIN 11

typedef struct
{
    uint8_t all[STATES];
    uint8_t any[STATES];
    uint8_t rise[RISE_READS];
    uint16_t samples;
} pad_sample_t;

typedef struct
{
    uint32_t time;
    uint8_t port;
    uint8_t state;
    uint8_t value;
    uint8_t reserved;
} pad_change_t;

typedef struct
{
    uint8_t ran;
    uint8_t change_count;
    uint8_t reserved[2];
    uint32_t live_time;
    pad_change_t changes[CHANGES];
    pad_sample_t prompts[2][PROMPTS];
} pads_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void sample(uint16_t port, uint8_t* values, uint8_t* rise);
static void live(pads_t* result);
static void series(pads_t* result, int port);
static int stable(const pad_sample_t* entry, int state);
static const pad_sample_t* idle_row(const pads_t* result, int port);
static uint8_t difference(const pad_sample_t* entry, const pad_sample_t* nothing, int state);
static int zoom_line(const pads_t* result, int port, char* line, int size);
static void draw_port(const pads_t* result, int port, int row);

static const test_step_t k_steps[] =
{
    { "LIVE VIEW, BOTH PORTS", TEST_PHOTO },
    { "PROMPTS, PORT 1", TEST_PHOTO },
    { "PROMPTS, PORT 2", TEST_PHOTO },
    { 0, 0 }
};

static const uint8_t k_outputs[STATES] = { 0x0F, 0x3F, 0x00, 0x30 };
static const char* const k_state_names[STATES] = { "C0", "C1", "T0", "T1" };

static const char* const k_prompts[PROMPTS] =
{
    "NOTHING", "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "RUN", "SELECT", "ZOOM", "ZOOM AND A", "NOTHING AGAIN"
};

static const char* const k_short[PROMPTS] =
{
    "NOTHING", "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "RUN", "SELECT", "ZOOM", "ZOOM+A", "AGAIN"
};

const test_t g_test_pad =
{
    "4.1", "PAD PORTS", "PAD", 0, 1, sizeof(pads_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    pads_t* result = (pads_t*)data;

    ui_pad_only(0);

    if ((steps & 0x01) != 0)
        live(result);

    if ((steps & 0x02) != 0)
        series(result, 0);

    if ((steps & 0x04) != 0)
        series(result, 1);

    out8(0x04D6, OUTPUT_IDLE);
    scr_clear_rows(SHELL_BODY_TOP, SHELL_BODY_BOTTOM);
}

// The four states in turn, 20 us each, after six reads straight after COM rises from the idle state
static void sample(uint16_t port, uint8_t* values, uint8_t* rise)
{
    out8(0x04D6, OUTPUT_IDLE);
    tb_delay(STATE_DELAY_US);
    out8(0x04D6, 0x3F);

    for (int i = 0; i < RISE_READS; i++)
        rise[i] = in8(port);

    for (int state = 0; state < STATES; state++)
    {
        out8(0x04D6, k_outputs[state]);
        tb_delay(STATE_DELAY_US);
        values[state] = in8(port);
    }

    out8(0x04D6, OUTPUT_IDLE);
}

// Both ports live with every change logged; A and B held together on either port for a second leave
static void live(pads_t* result)
{
    uint8_t last[2][STATES] = { { 0 } };
    uint32_t start = tb_now();
    uint32_t held = 0;
    uint32_t drawn = 0;

    shell_step("LIVE VIEW");
    ui_prompt("PRESS ANYTHING ON EITHER PAD.", "HOLD A AND B FOR 1 S TO LEAVE");
    result->ran |= 0x01;

    for (int port = 0; port < 2; port++)
    {
        uint8_t rise[RISE_READS];
        sample(port == 0 ? 0x04D0 : 0x04D2, last[port], rise);
    }

    while (tb_now() - start < LIVE_LIMIT_US)
    {
        uint32_t now = tb_now();
        int both = 0;

        for (int port = 0; port < 2; port++)
        {
            uint8_t values[STATES];
            uint8_t rise[RISE_READS];

            sample(port == 0 ? 0x04D0 : 0x04D2, values, rise);
            both |= (values[0] & 0x30) == 0;

            for (int state = 0; state < STATES; state++)
            {
                if (values[state] == last[port][state])
                    continue;

                pad_change_t* change = &result->changes[result->change_count % CHANGES];
                change->time = now - start;
                change->port = (uint8_t)port;
                change->state = (uint8_t)state;
                change->value = values[state];
                result->change_count = (uint8_t)(result->change_count < 255 ? result->change_count + 1 : 255);
                last[port][state] = values[state];
            }
        }

        if (!both)
            held = now;
        else if (now - held >= LEAVE_US)
            break;

        if (now - drawn >= 100000u)
        {
            drawn = now;

            for (int port = 0; port < 2; port++)
                ui_note(port, COLOR_WHITE, "PORT %u  C0 %02X C1 %02X T0 %02X T1 %02X", port + 1, last[port][0],
                    last[port][1], last[port][2], last[port][3]);

            ui_note(3, COLOR_GRAY, "%u CHANGES, %u S", result->change_count, (now - start) / 1000000u);
            scr_present();
        }
    }

    result->live_time = tb_now() - start;
}

static void series(pads_t* result, int port)
{
    uint16_t address = port == 0 ? 0x04D0 : 0x04D2;
    char text[40];

    shell_step("PROMPTS, PORT %u", port + 1);
    fmt_format(text, sizeof(text), "THE MARTY PAD IN PORT %u.", port + 1);
    ui_prompt(text, "HOLD WHAT EACH PROMPT NAMES FOR 3 S");

    if (ui_wait(15, "A STARTS, B SKIPS") == UI_B)
        return;

    result->ran |= (uint8_t)(0x02 << port);

    for (int i = 0; i < PROMPTS; i++)
    {
        pad_sample_t* entry = &result->prompts[port][i];
        uint32_t start = tb_now();
        uint32_t drawn = 0;

        shell_step("PORT %u, %s", port + 1, k_prompts[i]);
        fmt_format(text, sizeof(text), "PORT %u: HOLD %s", port + 1, k_prompts[i]);
        ui_prompt(text, i == 0 || i == AGAIN ? "HANDS OFF THE PAD" : "NOW, UNTIL THE PROMPT CHANGES");

        for (int state = 0; state < STATES; state++)
        {
            entry->all[state] = 0xFF;
            entry->any[state] = 0x00;
        }

        while (tb_now() - start < PROMPT_US)
        {
            uint8_t values[STATES];
            uint8_t rise[RISE_READS];
            uint32_t elapsed = tb_now() - start;

            sample(address, values, rise);

            if (elapsed >= SETTLE_US)
            {
                for (int state = 0; state < STATES; state++)
                {
                    entry->all[state] &= values[state];
                    entry->any[state] |= values[state];
                }

                if (entry->samples == 0)
                {
                    for (int k = 0; k < RISE_READS; k++)
                        entry->rise[k] = rise[k];
                }

                entry->samples++;
            }

            if (elapsed - drawn >= 100000u)
            {
                drawn = elapsed;
                ui_note(0, COLOR_WHITE, "C0 %02X C1 %02X T0 %02X T1 %02X", values[0], values[1], values[2], values[3]);
                ui_countdown(start, PROMPT_US, "");
            }
        }
    }
}

static int stable(const pad_sample_t* entry, int state)
{
    return entry->samples != 0 && entry->all[state] == entry->any[state];
}

static uint8_t difference(const pad_sample_t* entry, const pad_sample_t* nothing, int state)
{
    return (uint8_t)(entry->all[state] ^ nothing->all[state]);
}

// NOTHING AGAIN when it sampled, else NOTHING
static const pad_sample_t* idle_row(const pads_t* result, int port)
{
    return result->prompts[port][AGAIN].samples != 0 ? &result->prompts[port][AGAIN] : &result->prompts[port][0];
}

// Zoom's bits against NOTHING with COM low and high, each with the level it reads while pressed
static int zoom_line(const pads_t* result, int port, char* line, int size)
{
    const pad_sample_t* nothing = idle_row(result, port);
    const pad_sample_t* zoom = &result->prompts[port][ZOOM];
    uint8_t low = difference(zoom, nothing, 0);
    uint8_t high = difference(zoom, nothing, 1);
    uint8_t bits = low | high;
    int bit = -1;

    if (nothing->samples == 0 || zoom->samples == 0)
    {
        fmt_copy(line, "ZOOM NOT RUN", size);
        return 0;
    }

    for (int i = 7; i >= 0; i--)
    {
        if ((bits & (1 << i)) != 0)
            bit = i;
    }

    if (bit < 0)
    {
        fmt_copy(line, "ZOOM CHANGES NO BIT", size);
        return 1;
    }

    fmt_format(line, size, "ZOOM BIT %u ACTIVE %s, COM %s", bit,
        (zoom->all[(low & (1 << bit)) != 0 ? 0 : 1] & (1 << bit)) != 0 ? "HIGH" : "LOW",
        low != 0 && high != 0 ? "EITHER" : low != 0 ? "LOW ONLY" : "HIGH ONLY");
    return 1;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 3;
}

static void page(const void* data, uint32_t steps, int number)
{
    const pads_t* result = (const pads_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number < 2)
    {
        draw_port(result, number, row);
        return;
    }

    shell_line(row++, COLOR_CYAN, "LIVE VIEW, LAST CHANGES, MS");

    if ((result->ran & 0x01) == 0)
    {
        shell_line(row++, COLOR_GRAY, "NOT RUN");
        return;
    }

    int count = result->change_count < CHANGES ? result->change_count : CHANGES;
    int first = result->change_count > CHANGES ? result->change_count - CHANGES : 0;

    if (count > 18)
    {
        first += count - 18;
        count = 18;
    }

    for (int i = 0; i < count; i++)
    {
        const pad_change_t* change = &result->changes[(first + i) % CHANGES];
        shell_line(row++, COLOR_WHITE, "%7u  PORT %u %s %02X", change->time / 1000, change->port + 1,
            k_state_names[change->state], change->value);
    }

    shell_line(row++, COLOR_GRAY, "%u CHANGES IN %u S", result->change_count, result->live_time / 1000000u);
    shell_line(row++, COLOR_GRAY, "C COM, T TRIGGERS LOW; 0/1 COM LEVEL");
}

// One row per prompt: the four states (* when a bit moved while held) and the bits that differ from NOTHING
static void draw_port(const pads_t* result, int port, int row)
{
    const pad_sample_t* nothing = idle_row(result, port);
    const pad_sample_t* first = &result->prompts[port][0];
    char line[40];

    shell_line(row++, COLOR_CYAN, "PORT %u   C0  C1  T0  T1   D0 D1", port + 1);

    if ((result->ran & (0x02 << port)) == 0)
    {
        shell_line(row++, COLOR_GRAY, "NOT RUN");
        return;
    }

    for (int i = 0; i < PROMPTS; i++)
    {
        const pad_sample_t* entry = &result->prompts[port][i];
        char states[STATES][4];

        for (int state = 0; state < STATES; state++)
            fmt_format(states[state], sizeof(states[state]), "%02X%c", entry->all[state],
                stable(entry, state) ? ' ' : '*');

        shell_line(row++, i == ZOOM || i == ZOOM + 1 ? COLOR_YELLOW : COLOR_WHITE, "%-8s %s %s %s %s  %02X %02X",
            k_short[i], states[0], states[1], states[2], states[3], difference(entry, nothing, 0),
            difference(entry, nothing, 1));
    }

    row++;
    zoom_line(result, port, line, sizeof(line));
    shell_line(row++, COLOR_GREEN, "%s", line);
    shell_line(row++, COLOR_WHITE, "BIT 6 ECHOES COM: %s",
        nothing->samples == 0 ? "-" : ((nothing->all[0] & 0x40) == 0 && (nothing->all[1] & 0x40) != 0) ? "YES" : "NO");
    shell_line(row++, COLOR_WHITE, "AFTER COM RISES: %02X %02X %02X %02X %02X %02X", nothing->rise[0], nothing->rise[1],
        nothing->rise[2], nothing->rise[3], nothing->rise[4], nothing->rise[5]);
    shell_line(row++, COLOR_WHITE, "TRIG LOW HIDES A: %s",
        result->prompts[port][5].samples == 0 ? "-" :
        (difference(&result->prompts[port][5], nothing, 2) & 0x10) == 0 ? "YES" : "NO");

    if (first->samples != 0 && nothing != first && (difference(first, nothing, 0) | difference(first, nothing, 1)) != 0)
        shell_line(row++, COLOR_YELLOW, "NOTHING DIFFERS FROM AGAIN: %02X %02X", difference(first, nothing, 0),
            difference(first, nothing, 1));

    shell_line(row++, COLOR_GRAY, "C COM, T TRIG LOW, D BITS VS AGAIN");
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const pads_t* result = (const pads_t*)data;
    (void)steps;

    *color = COLOR_WHITE;

    for (int port = 0; port < 2; port++)
    {
        if ((result->ran & (0x02 << port)) != 0 && zoom_line(result, port, line, 34))
            return;
    }

    fmt_format(line, 34, "%u LIVE CHANGES", result->change_count);
}
