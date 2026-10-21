#include "tests.h"

// 4.3 Keyboard (docs/12, HT-20): the Towns keyboard, the pad in port 1 for the prompts. IRQ1 off (0604h = 0) unless a
// step says otherwise. A drain reads 0600h whenever 0602h bit 0 is set and goes on until nothing came for a while, so
// bytes still crossing the serial link are counted; every byte keeps its time and the 0602h value before it

#define LOG 256
#define SMALL_LOG 64
#define REPLY 16
#define COMMAND_CASES 3
#define SEEN 8
#define QUIET_US 200000u
#define IBF_TIMEOUT_US 20000u
#define REPLY_US 500000u
#define PACING_US 10000000u
#define REPEAT_GAPS 3
#define KEY_Q 0x11
#define KEY_0 0x0B
#define KEY_A 0x1E

typedef struct
{
    uint16_t count;
    uint8_t status_or;
    uint8_t status_and;
    uint32_t times[LOG];
    uint8_t bytes[LOG];
    uint8_t statuses[LOG];
} kb_log_t;

typedef struct
{
    uint16_t count;
    uint8_t reserved[2];
    uint32_t times[SMALL_LOG];
    uint8_t bytes[SMALL_LOG];
} kb_live_t;

typedef struct
{
    uint8_t count;
    uint8_t irr_after;
    uint16_t ibf_us;
    uint16_t times[REPLY];
    uint8_t bytes[REPLY];
} kb_reply_t;

typedef struct
{
    uint8_t ran;
    uint8_t seen_count;
    uint8_t seen[SEEN];
    uint8_t mask_irr[5];
    uint8_t mask_cause[2];
    uint8_t mask_pending;
    uint8_t mask_ran;
    uint8_t reserved;
    uint32_t first_byte_time;
    uint32_t repeat_gaps[REPEAT_GAPS];
    uint16_t drained[REPEAT_GAPS];
    uint16_t reserved2;
    kb_log_t depth;
    kb_log_t overflow;
    kb_log_t breaks;
    kb_log_t pacing;
    kb_live_t typematic;
    kb_live_t fast;
    kb_reply_t replies[COMMAND_CASES];
} keyboard_t;

typedef struct
{
    uint16_t makes;
    uint16_t breaks;
    uint16_t repeats;
    uint16_t other;
    uint8_t first_make;
    uint8_t last_make;
} kb_counts_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void note_status(keyboard_t* result, uint8_t status);
static void drain(keyboard_t* result, kb_log_t* log, uint32_t quiet);
static void flush(void);
static int command(uint8_t value, uint16_t port, uint32_t* ibf);
static void live_read(kb_live_t* log, uint32_t microseconds);
static int ask(const char* first, const char* second, uint32_t seconds);
static void hold(const char* first, const char* second, uint32_t microseconds);
static void step_depth(keyboard_t* result);
static void step_overflow(keyboard_t* result);
static void step_breaks(keyboard_t* result);
static void step_typematic(keyboard_t* result);
static void step_pacing(keyboard_t* result);
static void step_commands(keyboard_t* result);
static void step_masking(keyboard_t* result);
static void pacing_irq(int irq);
static void count_messages(const uint8_t* bytes, int count, kb_counts_t* counts);
static uint32_t typematic_delay(const kb_live_t* log, uint32_t* interval);
static uint32_t rearm(const kb_log_t* log, uint32_t* fastest);
static int draw_log_counts(int row, const char* name, const kb_log_t* log);

static const test_step_t k_steps[] =
{
    { "DEPTH, MASKED", TEST_PHOTO },
    { "OVERFLOW, A HELD 5 S", TEST_PHOTO },
    { "FOUR BREAKS", TEST_PHOTO },
    { "TYPEMATIC AND DRAIN", TEST_PHOTO },
    { "IRQ1 PACING", TEST_PHOTO },
    { "COMMANDS A0H A1H", 0 },
    { "MASKING A PENDING IRQ", TEST_PHOTO },
    { 0, 0 }
};

static const char* const k_command_names[COMMAND_CASES] = { "A1H TO 0602H", "A0H, A1H TO 0602H", "A1H TO 0600H" };

static kb_log_t* s_pacing;

const test_t g_test_keyboard =
{
    "4.3", "KEYBOARD", "KEYBOARD", 0, 1, sizeof(keyboard_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    keyboard_t* result = (keyboard_t*)data;

    ui_pad_only(1);
    out8(0x0604, 0x00);
    pic_mask(1);
    ui_prompt("THE TOWNS KEYBOARD PLUGGED IN,", "THE PAD IN PORT 1 FOR THE PROMPTS");

    if (ui_wait(30, "A STARTS, B SKIPS") != UI_A)
        return;

    flush();

    if ((steps & 0x01) != 0)
        step_depth(result);

    if ((steps & 0x02) != 0)
        step_overflow(result);

    if ((steps & 0x04) != 0)
        step_breaks(result);

    if ((steps & 0x08) != 0)
        step_typematic(result);

    if ((steps & 0x10) != 0)
        step_pacing(result);

    if ((steps & 0x20) != 0)
        step_commands(result);

    if ((steps & 0x40) != 0)
        step_masking(result);

    out8(0x0604, 0x00);
    flush();
    scr_clear_rows(SHELL_BODY_TOP, SHELL_BODY_BOTTOM);
}

static void note_status(keyboard_t* result, uint8_t status)
{
    for (int i = 0; i < result->seen_count; i++)
    {
        if (result->seen[i] == status)
            return;
    }

    if (result->seen_count < SEEN)
        result->seen[result->seen_count++] = status;
}

static void drain(keyboard_t* result, kb_log_t* log, uint32_t quiet)
{
    uint32_t start = tb_now();
    uint32_t last = start;

    log->count = 0;
    log->status_or = 0x00;
    log->status_and = 0xFF;

    while (tb_now() - last < quiet && log->count < LOG)
    {
        uint8_t status = in8(0x0602);

        log->status_or |= status;
        log->status_and &= status;
        note_status(result, status);

        if ((status & 0x01) == 0)
            continue;

        uint8_t value = in8(0x0600);
        last = tb_now();
        log->times[log->count] = last - start;
        log->bytes[log->count] = value;
        log->statuses[log->count] = status;
        log->count++;
    }
}

static void flush(void)
{
    uint32_t last = tb_now();

    while (tb_now() - last < 50000u)
    {
        if ((in8(0x0602) & 0x01) != 0)
        {
            (void)in8(0x0600);
            last = tb_now();
        }
    }
}

// A command or parameter byte once IBF (0602h bit 1) is clear; the time IBF stayed set after it
static int command(uint8_t value, uint16_t port, uint32_t* ibf)
{
    uint32_t start = tb_now();

    while ((in8(0x0602) & 0x02) != 0)
    {
        if (tb_now() - start > IBF_TIMEOUT_US)
            return 0;
    }

    out8(port, value);
    start = tb_now();

    while ((in8(0x0602) & 0x02) != 0 && tb_now() - start < IBF_TIMEOUT_US)
        ;

    if (ibf != 0)
        *ibf = tb_now() - start;

    return 1;
}

// Every byte read as soon as it is there, with its time
static void live_read(kb_live_t* log, uint32_t microseconds)
{
    uint32_t start = tb_now();

    log->count = 0;

    while (tb_now() - start < microseconds && log->count < SMALL_LOG)
    {
        if ((in8(0x0602) & 0x01) == 0)
            continue;

        log->bytes[log->count] = in8(0x0600);
        log->times[log->count] = tb_now() - start;
        log->count++;
    }
}

static int ask(const char* first, const char* second, uint32_t seconds)
{
    ui_prompt(first, second);
    return ui_wait(seconds, "A WHEN DONE, B SKIPS");
}

static void hold(const char* first, const char* second, uint32_t microseconds)
{
    uint32_t start = tb_now();

    ui_prompt(first, second);

    while (tb_now() - start < microseconds)
        ui_countdown(start, microseconds, "");
}

static void step_depth(keyboard_t* result)
{
    shell_step("DEPTH");
    flush();

    if (ask("TYPE Q TO P, A TO L, Z TO M, 1 TO 0", "ONE KEY AT A TIME, THEN A ON THE PAD", 60) == UI_B)
        return;

    drain(result, &result->depth, QUIET_US);
    result->ran |= 0x01;
}

static void step_overflow(keyboard_t* result)
{
    shell_step("OVERFLOW");
    flush();

    if (ask("PRESS A ON THE PAD, THEN HOLD THE", "A KEY UNTIL TOLD TO RELEASE IT", 30) != UI_A)
        return;

    hold("HOLD THE A KEY", "", 5000000u);
    hold("RELEASE THE A KEY NOW", "", 1500000u);
    drain(result, &result->overflow, QUIET_US);
    result->ran |= 0x02;
}

static void step_breaks(keyboard_t* result)
{
    shell_step("FOUR BREAKS");
    flush();

    if (ask("HOLD A, THEN S, D AND F TOO, THEN", "RELEASE THEM ALL; A ON THE PAD", 30) == UI_B)
        return;

    tb_delay(500000u);
    drain(result, &result->breaks, QUIET_US);
    result->ran |= 0x04;
}

// S held and read live gives the delay and the interval; A held unread fills the buffer, then each drain is timed to
// the next repeat; D held after AB (300 ms) and AE (20 ms), then A1 puts the defaults back
static void step_typematic(keyboard_t* result)
{
    shell_step("TYPEMATIC");
    flush();

    if (ask("PRESS A ON THE PAD, THEN HOLD THE", "S KEY FOR 3 S", 30) != UI_A)
        return;

    ui_prompt("HOLD THE S KEY", "");
    live_read(&result->typematic, 4000000u);
    hold("RELEASE THE S KEY", "", 1000000u);
    flush();

    if (ask("PRESS A ON THE PAD, THEN HOLD THE", "A KEY UNTIL TOLD TO RELEASE IT", 30) == UI_A)
    {
        uint32_t start = tb_now();

        ui_prompt("HOLD THE A KEY", "");

        while ((in8(0x0602) & 0x01) == 0 && tb_now() - start < 10000000u)
            ;

        result->first_byte_time = tb_now() - start;
        tb_delay(2000000u);

        for (int i = 0; i < REPEAT_GAPS; i++)
        {
            uint32_t last = tb_now();
            uint16_t count = 0;

            while (tb_now() - last < 3000u)
            {
                if ((in8(0x0602) & 0x01) != 0)
                {
                    (void)in8(0x0600);
                    last = tb_now();
                    count++;
                }
            }

            while ((in8(0x0602) & 0x01) == 0 && tb_now() - last < 2000000u)
                ;

            result->repeat_gaps[i] = tb_now() - last;
            result->drained[i] = count;
            tb_delay(300000u);
        }

        hold("RELEASE THE A KEY NOW", "", 1000000u);
        flush();
    }

    command(0xAB, 0x0602, 0);
    command(0xAE, 0x0602, 0);

    if (ask("PRESS A ON THE PAD, THEN HOLD THE", "D KEY FOR 3 S", 30) == UI_A)
    {
        ui_prompt("HOLD THE D KEY", "");
        live_read(&result->fast, 4000000u);
        hold("RELEASE THE D KEY", "", 1000000u);
    }

    command(0xA1, 0x0602, 0);
    flush();
    result->ran |= 0x08;
}

static void pacing_irq(int irq)
{
    uint8_t value = in8(0x0600);
    uint8_t status = in8(0x0602);
    uint32_t now = tb_now();
    kb_log_t* log = s_pacing;

    if (log->count < LOG)
    {
        log->times[log->count] = now;
        log->bytes[log->count] = value;
        log->statuses[log->count] = status;
        log->count++;
    }

    pic_eoi(irq);
}

// IRQ1 on; the handler reads one byte, then 0602h, and keeps the time
static void step_pacing(keyboard_t* result)
{
    uint32_t start;
    uint32_t now;

    shell_step("IRQ1 PACING");
    flush();

    if (ask("PRESS A ON THE PAD, THEN TYPE FAST", "WITH SEVERAL FINGERS FOR 10 S", 30) != UI_A)
        return;

    ui_prompt("TYPE FAST WITH SEVERAL FINGERS", "UNTIL THIS PROMPT GOES");
    s_pacing = &result->pacing;
    result->pacing.count = 0;
    cpu_set_irq_handler(1, pacing_irq);
    pic_unmask(1);
    out8(0x0604, 0x01);
    start = tb_now();
    sti();

    do
    {
        for (volatile int i = 0; i < 200; i++)
            ;

        cli();
        now = tb_now();
        sti();
    }
    while (now - start < PACING_US);

    cli();
    out8(0x0604, 0x00);
    pic_mask(1);
    cpu_set_irq_handler(1, 0);

    for (int i = 0; i < result->pacing.count; i++)
        result->pacing.times[i] -= start;

    hold("STOP TYPING", "", 1000000u);
    flush();
    result->ran |= 0x10;
}

// IRQ1 enabled at 0604h but masked at the PIC, so IRR shows whether a reply leaves the request up
static void step_commands(keyboard_t* result)
{
    shell_step("COMMANDS");
    ui_prompt("KEYBOARD COMMANDS, HANDS OFF", "");

    for (int c = 0; c < COMMAND_CASES; c++)
    {
        kb_reply_t* reply = &result->replies[c];
        uint32_t ibf = 0;
        uint32_t start;

        flush();
        out8(0x0604, 0x01);

        if (c == 1)
        {
            command(0xA0, 0x0602, 0);
            tb_delay(1000u);
        }

        command(0xA1, c == 2 ? 0x0600 : 0x0602, &ibf);
        reply->ibf_us = (uint16_t)(ibf < 0xFFFF ? ibf : 0xFFFF);
        start = tb_now();

        while (tb_now() - start < 50000u && (in8(0x0602) & 0x01) == 0)
            ;

        reply->irr_after = (uint8_t)pic_read_irr();
        start = tb_now();

        while (tb_now() - start < REPLY_US && reply->count < REPLY)
        {
            if ((in8(0x0602) & 0x01) == 0)
                continue;

            reply->bytes[reply->count] = in8(0x0600);
            reply->times[reply->count] = (uint16_t)((tb_now() - start) / 100u);
            reply->count++;
        }

        out8(0x0604, 0x00);
    }

    flush();
    result->ran |= 0x20;
}

// A key message pending with IRQ1 requested, masked at the PIC: does 0604h = 0 drop the request at once?
static void step_masking(keyboard_t* result)
{
    uint32_t start;

    shell_step("MASKING");
    flush();
    out8(0x0604, 0x01);
    ui_prompt("PRESS AND RELEASE ANY KEY", "");
    start = tb_now();

    while ((pic_read_irr() & 0x0002) == 0 && tb_now() - start < 20000000u)
        ui_countdown(start, 20000000u, "");

    result->mask_ran = (pic_read_irr() & 0x0002) != 0;
    result->mask_irr[0] = (uint8_t)pic_read_irr();
    result->mask_cause[0] = in8(0x0604);
    out8(0x0604, 0x00);
    result->mask_irr[1] = (uint8_t)pic_read_irr();
    tb_delay(100);
    result->mask_irr[2] = (uint8_t)pic_read_irr();
    result->mask_cause[1] = in8(0x0604);
    out8(0x0604, 0x01);
    result->mask_irr[3] = (uint8_t)pic_read_irr();
    result->mask_pending = in8(0x0602);
    (void)in8(0x0600);
    result->mask_irr[4] = (uint8_t)pic_read_irr();
    out8(0x0604, 0x00);
    tb_delay(500000u);
    flush();
    result->ran |= 0x40;
}

static void count_messages(const uint8_t* bytes, int count, kb_counts_t* counts)
{
    memset(counts, 0, sizeof(*counts));

    for (int i = 0; i < count; i++)
    {
        if ((bytes[i] & 0x80) == 0 || i + 1 >= count || (bytes[i + 1] & 0x80) != 0)
        {
            counts->other++;
            continue;
        }

        uint8_t kind = bytes[i] & 0xF0;
        uint8_t code = bytes[i + 1];

        if (kind == 0xF0)
            counts->repeats++;
        else if ((kind & 0x10) != 0)
            counts->breaks++;
        else
        {
            if (counts->makes == 0)
                counts->first_make = code;

            counts->last_make = code;
            counts->makes++;
        }

        i++;
    }
}

// The first repeat after the make and the median gap between repeats, from the live log
static uint32_t typematic_delay(const kb_live_t* log, uint32_t* interval)
{
    uint32_t times[SMALL_LOG];
    int count = 0;

    for (int i = 0; i + 1 < log->count; i++)
    {
        if ((log->bytes[i] & 0x80) != 0 && (log->bytes[i + 1] & 0x80) == 0)
            times[count++] = log->times[i];
    }

    *interval = 0;

    if (count < 2)
        return 0;

    if (count >= 4)
    {
        uint32_t gaps[SMALL_LOG];

        for (int i = 2; i < count; i++)
            gaps[i - 2] = times[i] - times[i - 1];

        for (int i = 0; i < count - 2; i++)
        {
            for (int j = i + 1; j < count - 2; j++)
            {
                if (gaps[j] < gaps[i])
                {
                    uint32_t swap = gaps[i];
                    gaps[i] = gaps[j];
                    gaps[j] = swap;
                }
            }
        }

        *interval = gaps[(count - 2) / 2];
    }

    return times[1] - times[0];
}

// The median gap from an IRQ whose read left bytes behind to the next IRQ, and the fastest
static uint32_t rearm(const kb_log_t* log, uint32_t* fastest)
{
    uint32_t gaps[LOG];
    int count = 0;

    for (int i = 0; i + 1 < log->count; i++)
    {
        if ((log->statuses[i] & 0x01) != 0)
            gaps[count++] = log->times[i + 1] - log->times[i];
    }

    *fastest = 0;

    if (count == 0)
        return 0;

    for (int i = 0; i < count; i++)
    {
        for (int j = i + 1; j < count; j++)
        {
            if (gaps[j] < gaps[i])
            {
                uint32_t swap = gaps[i];
                gaps[i] = gaps[j];
                gaps[j] = swap;
            }
        }
    }

    *fastest = gaps[0];
    return gaps[count / 2];
}

static int draw_log_counts(int row, const char* name, const kb_log_t* log)
{
    kb_counts_t counts;

    count_messages(log->bytes, log->count, &counts);
    shell_line(row++, COLOR_WHITE, "%s: %u BYTES IN %u MS", name, log->count,
        log->count != 0 ? log->times[log->count - 1] / 1000 : 0);
    shell_line(row++, COLOR_WHITE, " MAKE %u BREAK %u REPEAT %u OTHER %u", counts.makes, counts.breaks, counts.repeats,
        counts.other);
    return row;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 5;
}

static void page(const void* data, uint32_t steps, int number)
{
    const keyboard_t* result = (const keyboard_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        kb_counts_t counts;
        char line[40];
        int length = 0;

        shell_line(row++, COLOR_CYAN, "DEPTH: KEYS TYPED WHILE MASKED");

        if ((result->ran & 0x01) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        count_messages(result->depth.bytes, result->depth.count, &counts);
        row = draw_log_counts(row, "DRAIN", &result->depth);
        shell_line(row++, COLOR_GREEN, "DEPTH %u BYTES", result->depth.count);
        shell_line(row++, COLOR_GREEN, "FIRST MAKE %02X (Q 11), LAST %02X (0 0B)", counts.first_make,
            counts.last_make);
        shell_line(row++, COLOR_WHITE, "KEEPS %s",
            counts.first_make == KEY_Q && counts.last_make != KEY_0 ? "OLDEST" :
            counts.first_make != KEY_Q && counts.last_make == KEY_0 ? "NEWEST" :
            counts.first_make == KEY_Q ? "ALL" : "?");
        shell_line(row++, COLOR_WHITE, "0602H OR %02X AND %02X", result->depth.status_or, result->depth.status_and);
        shell_line(row++, COLOR_CYAN, "BYTES, MS");

        for (int i = 0; i < result->depth.count && row <= SHELL_BODY_BOTTOM - 1; i++)
        {
            length += fmt_format(line + length, (int)sizeof(line) - length, "%02X ", result->depth.bytes[i]);

            if (length >= 36 || i + 1 == result->depth.count)
            {
                shell_line(row++, COLOR_WHITE, "%s", line);
                length = 0;
            }
        }

        return;
    }

    if (number == 1)
    {
        kb_counts_t counts;

        shell_line(row++, COLOR_CYAN, "OVERFLOW AND BREAKS");

        if ((result->ran & 0x02) != 0)
        {
            int last_break = -1;

            row = draw_log_counts(row, "A HELD 5 S", &result->overflow);

            for (int i = 0; i + 1 < result->overflow.count; i++)
            {
                if ((result->overflow.bytes[i] & 0xF0) == 0xB0 && result->overflow.bytes[i + 1] == KEY_A)
                    last_break = i;
            }

            shell_line(row++, COLOR_GREEN, "A BREAK %s", last_break >= 0 ? "ARRIVED" : "LOST");
            shell_line(row++, COLOR_WHITE, " LAST 8: %02X %02X %02X %02X %02X %02X %02X %02X",
                result->overflow.count >= 8 ? result->overflow.bytes[result->overflow.count - 8] : 0,
                result->overflow.count >= 7 ? result->overflow.bytes[result->overflow.count - 7] : 0,
                result->overflow.count >= 6 ? result->overflow.bytes[result->overflow.count - 6] : 0,
                result->overflow.count >= 5 ? result->overflow.bytes[result->overflow.count - 5] : 0,
                result->overflow.count >= 4 ? result->overflow.bytes[result->overflow.count - 4] : 0,
                result->overflow.count >= 3 ? result->overflow.bytes[result->overflow.count - 3] : 0,
                result->overflow.count >= 2 ? result->overflow.bytes[result->overflow.count - 2] : 0,
                result->overflow.count >= 1 ? result->overflow.bytes[result->overflow.count - 1] : 0);
            shell_line(row++, COLOR_WHITE, " FIRST 4 MS: %u %u %u %u", result->overflow.times[0] / 1000,
                result->overflow.times[1] / 1000, result->overflow.times[2] / 1000, result->overflow.times[3] / 1000);
        }
        else
            shell_line(row++, COLOR_GRAY, "OVERFLOW NOT RUN");

        row++;

        if ((result->ran & 0x04) != 0)
        {
            int found = 0;

            row = draw_log_counts(row, "A S D F", &result->breaks);
            count_messages(result->breaks.bytes, result->breaks.count, &counts);

            for (int key = 0; key < 4; key++)
            {
                for (int i = 0; i + 1 < result->breaks.count; i++)
                {
                    if ((result->breaks.bytes[i] & 0xF0) == 0xB0 && result->breaks.bytes[i + 1] == KEY_A + key)
                    {
                        found++;
                        break;
                    }
                }
            }

            shell_line(row++, COLOR_GREEN, "BREAKS OF A S D F: %u OF 4", found);

            for (int i = 0; i < result->breaks.count && i < 24 && row <= SHELL_BODY_BOTTOM; i += 8)
            {
                char line[40];
                int length = 0;

                for (int k = i; k < i + 8 && k < result->breaks.count; k++)
                    length += fmt_format(line + length, (int)sizeof(line) - length, "%02X ", result->breaks.bytes[k]);

                shell_line(row++, COLOR_WHITE, " %s", line);
            }
        }
        else
            shell_line(row++, COLOR_GRAY, "BREAKS NOT RUN");

        return;
    }

    if (number == 2)
    {
        uint32_t interval;
        uint32_t delay;

        shell_line(row++, COLOR_CYAN, "TYPEMATIC, MS");

        if ((result->ran & 0x08) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        delay = typematic_delay(&result->typematic, &interval);
        shell_line(row++, COLOR_GREEN, "DEFAULT: DELAY %u.%u INTERVAL %u.%u", delay / 1000, delay / 100 % 10,
            interval / 1000, interval / 100 % 10);
        delay = typematic_delay(&result->fast, &interval);
        shell_line(row++, COLOR_GREEN, "ABH AEH: DELAY %u.%u INTERVAL %u.%u", delay / 1000, delay / 100 % 10,
            interval / 1000, interval / 100 % 10);
        shell_line(row++, COLOR_WHITE, "BYTES LIVE %u, AFTER ABH AEH %u", result->typematic.count, result->fast.count);
        row++;
        shell_line(row++, COLOR_CYAN, "A HELD UNREAD, DRAINED, NEXT REPEAT");
        shell_line(row++, COLOR_WHITE, "FIRST BYTE %u MS AFTER THE PROMPT", result->first_byte_time / 1000);

        for (int i = 0; i < REPEAT_GAPS; i++)
            shell_line(row++, COLOR_WHITE, "DRAIN %u: %3u BYTES, NEXT IN %u.%u", i + 1, result->drained[i],
                result->repeat_gaps[i] / 1000, result->repeat_gaps[i] / 100 % 10);

        row++;
        shell_line(row++, COLOR_CYAN, "LIVE BYTES, MS");

        for (int i = 0; i < result->typematic.count && i < 24 && row <= SHELL_BODY_BOTTOM; i += 4)
        {
            char line[40];
            int length = 0;

            for (int k = i; k < i + 4 && k < result->typematic.count; k++)
                length += fmt_format(line + length, (int)sizeof(line) - length, "%02X %4u ",
                    result->typematic.bytes[k], result->typematic.times[k] / 1000);

            shell_line(row++, COLOR_WHITE, "%s", line);
        }

        return;
    }

    if (number == 3)
    {
        uint32_t fastest;
        uint32_t median;

        shell_line(row++, COLOR_CYAN, "IRQ1 PACING, ONE BYTE AN IRQ");

        if ((result->ran & 0x10) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        median = rearm(&result->pacing, &fastest);
        shell_line(row++, COLOR_WHITE, "%u IRQS IN 10 S", result->pacing.count);
        shell_line(row++, COLOR_GREEN, "REARM %u US, FASTEST %u US", median, fastest);
        shell_line(row++, COLOR_GRAY, "GAPS AFTER A READ THAT LEFT BYTES");
        row++;
        shell_line(row++, COLOR_CYAN, "BYTE, 0602H AFTER, US SINCE LAST");

        for (int i = 0; i < result->pacing.count && row <= SHELL_BODY_BOTTOM; i += 3)
        {
            char line[40];
            int length = 0;

            for (int k = i; k < i + 3 && k < result->pacing.count; k++)
            {
                uint32_t gap = k > 0 ? result->pacing.times[k] - result->pacing.times[k - 1] : 0;
                length += fmt_format(line + length, (int)sizeof(line) - length, "%02X %02X %5u ",
                    result->pacing.bytes[k], result->pacing.statuses[k], gap < 99999 ? gap : 99999);
            }

            shell_line(row++, COLOR_WHITE, "%s", line);
        }

        return;
    }

    shell_line(row++, COLOR_CYAN, "COMMANDS: IBF US, IRR, REPLY@MS");

    if ((result->ran & 0x20) != 0)
    {
        for (int c = 0; c < COMMAND_CASES; c++)
        {
            const kb_reply_t* reply = &result->replies[c];
            char line[40];
            int length = 0;

            shell_line(row++, COLOR_WHITE, "%s: IBF %u IRR %02X", k_command_names[c], reply->ibf_us, reply->irr_after);

            for (int i = 0; i < reply->count && i < 4; i++)
                length += fmt_format(line + length, (int)sizeof(line) - length, "%02X@%u.%u ", reply->bytes[i],
                    reply->times[i] / 10, reply->times[i] % 10);

            shell_line(row++, COLOR_WHITE, " %u: %s", reply->count, reply->count != 0 ? line : "NONE");
        }
    }
    else
        shell_line(row++, COLOR_GRAY, "NOT RUN");

    row++;
    shell_line(row++, COLOR_CYAN, "MASKING A PENDING IRQ, IRR");

    if ((result->ran & 0x40) != 0)
    {
        shell_line(row++, COLOR_WHITE, "%s, 0604H %02X", result->mask_ran ? "REQUESTED" : "NO KEY", result->mask_cause[0]);
        shell_line(row++, COLOR_WHITE, "0604H=0: %02X, 100 US %02X, 0604H %02X", result->mask_irr[1],
            result->mask_irr[2], result->mask_cause[1]);
        shell_line(row++, COLOR_WHITE, "0604H=1: %02X, READ: %02X (0602H %02X)", result->mask_irr[3],
            result->mask_irr[4], result->mask_pending);
        shell_line(row++, COLOR_GREEN, "MASK DROPS IRQ: %s",
            !result->mask_ran ? "-" : (result->mask_irr[1] & 0x02) == 0 ? "AT ONCE" :
            (result->mask_irr[2] & 0x02) == 0 ? "WITHIN 100 US" : "NO");
    }
    else
        shell_line(row++, COLOR_GRAY, "NOT RUN");

    row++;
    shell_line(row++, COLOR_CYAN, "0602H VALUES SEEN");

    if (result->seen_count != 0)
    {
        char line[40];
        int length = 0;

        for (int i = 0; i < result->seen_count; i++)
            length += fmt_format(line + length, (int)sizeof(line) - length, "%02X ", result->seen[i]);

        shell_line(row++, COLOR_WHITE, "%s", line);
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const keyboard_t* result = (const keyboard_t*)data;
    uint32_t fastest;
    (void)steps;

    *color = COLOR_WHITE;

    if ((result->ran & 0x01) != 0 && (result->ran & 0x10) != 0)
        fmt_format(line, 34, "DEPTH %u B, REARM %u US", result->depth.count, rearm(&result->pacing, &fastest));
    else if ((result->ran & 0x01) != 0)
        fmt_format(line, 34, "DEPTH %u BYTES", result->depth.count);
    else
        fmt_copy(line, result->ran != 0 ? "SOME STEPS RUN" : "NOT RUN", 34);
}
