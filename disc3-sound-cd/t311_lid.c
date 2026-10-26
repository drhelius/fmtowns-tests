#include "tests.h"

// 3.11 Lid and disc change (docs/12, HT-51): needs someone at the drive. The screen says when to open and close the
// lid; 80h+STATUS every 100 ms logs each answer that differs from the one before. A read with the lid open, the first
// reads after closing it, the time from closing to a good read (up to eight times), the lid opened during CD-DA, and a
// TOC read as the first command after a change, then 80h. Expected: 00 09 while not ready, 21 08 once at the first
// access after the change

#define CHANGES 48
#define CDDA_CHANGES 32
#define SPINS 8
#define POLL_US 100000u
#define WATCH_US 60000000u
#define READ_LIMIT_US 30000000u

typedef struct
{
    uint8_t change_count;
    uint8_t cdda_count;
    uint8_t closed_attempts;
    uint8_t spin_count;
    cd_packet_t changes[CHANGES];
    cd_keep_t open_read;
    cd_keep_t closed_first;
    uint32_t closed_time;
    uint32_t spin[SPINS];
    uint32_t spin_attempts[SPINS];
    cd_packet_t cdda[CDDA_CHANGES];
    cd_keep_t toc_first;
    cd_keep_t toc_answer;
    cd_keep_t state_after;
    uint8_t toc_attempts;
    uint8_t toc_total;
    uint8_t reserved[2];
} lid_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void prompt(const char* first, const char* second);
static int wait_button(void);
static int pressed(void);
static void watch(cd_packet_t* changes, uint8_t* count, int capacity, int subq);
static uint32_t read_until_good(uint32_t* attempts, cd_keep_t* first);
static void toc_after_change(lid_t* result);
static int not_ready(const cd_log_t* log);

static const test_step_t k_steps[] =
{
    { "OPEN AND CLOSE, 80H", 0 },
    { "READS, LID OPEN, CLOSED", 0 },
    { "SPIN-UP TIME X8", 0 },
    { "LID OPEN DURING CD-DA", 0 },
    { "CHANGE, THEN A TOC READ", 0 },
    { 0, 0 }
};

const test_t g_test_lid =
{
    "3.11", "LID AND DISC CHANGE", "LID", 0, 2, sizeof(lid_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    lid_t* result = (lid_t*)data;
    cd_log_t log;

    cd_quiet();

    if ((steps & 0x01) != 0)
    {
        shell_step("OPEN AND CLOSE");
        prompt("OPEN THE LID, WAIT 5 S, CLOSE IT,", "THEN PRESS A ONCE THE DISC SPINS");
        watch(result->changes, &result->change_count, CHANGES, 0);
    }

    if ((steps & 0x02) != 0)
    {
        shell_step("READ WITH THE LID OPEN");
        prompt("OPEN THE LID AND PRESS A", "(B SKIPS)");

        if (wait_button())
        {
            cd_read(&log, CD_READ | CD_STATUS, CD_PAD_LBA, 1);
            cd_keep(&result->open_read, &log);
            prompt("CLOSE THE LID AND PRESS A AT ONCE", "");

            if (wait_button())
            {
                uint32_t attempts = 0;
                result->closed_time = read_until_good(&attempts, &result->closed_first);
                result->closed_attempts = (uint8_t)(attempts < 255 ? attempts : 255);
            }
        }
    }

    if ((steps & 0x04) != 0)
    {
        for (int i = 0; i < SPINS; i++)
        {
            shell_step("SPIN-UP %u/%u", i + 1, SPINS);
            prompt("OPEN THE LID, THEN CLOSE IT AND", "PRESS A AT ONCE (B ENDS)");

            if (!wait_button())
                break;

            result->spin[i] = read_until_good(&result->spin_attempts[i], 0);
            result->spin_count = (uint8_t)(i + 1);
        }
    }

    if ((steps & 0x08) != 0)
    {
        shell_step("LID OPEN DURING CD-DA");
        cd_load_toc(&log);
        cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
        prompt("TRACK 7 PLAYS: OPEN THE LID, CLOSE", "IT AFTER 5 S, PRESS A WHEN IT SPINS");
        watch(result->cdda, &result->cdda_count, CDDA_CHANGES, 1);
    }

    if ((steps & 0x10) != 0)
    {
        shell_step("CHANGE, THEN A TOC READ");
        cd_quiet();
        prompt("OPEN THE LID, WAIT 5 S, CLOSE IT,", "THEN PRESS A ONCE THE DISC SPINS");

        if (wait_button())
            toc_after_change(result);
    }

    scr_clear_rows(SHELL_BODY_TOP, SHELL_BODY_BOTTOM);
    cd_quiet();
}

static void prompt(const char* first, const char* second)
{
    scr_clear_rows(SHELL_BODY_TOP, SHELL_BODY_BOTTOM);
    shell_line(SHELL_BODY_TOP + 2, COLOR_YELLOW, "%s", first);
    shell_line(SHELL_BODY_TOP + 3, COLOR_YELLOW, "%s", second);
    scr_present();
}

static int wait_button(void)
{
    for (;;)
    {
        int event = input_wait();

        if (event == INPUT_A)
            return 1;

        if (event == INPUT_B)
            return 0;
    }
}

static int pressed(void)
{
    int event = input_poll();
    return event == INPUT_A || event == INPUT_B;
}

// 80h every 100 ms until A, B or a minute; each answer that differs from the last is kept with its time, and with
// subq set a sub-Q position every second as well, as a packet 06 valid MM SS of the absolute time
static void watch(cd_packet_t* changes, uint8_t* count, int capacity, int subq)
{
    uint8_t last[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    uint32_t start = tb_now();
    uint32_t next_subq = 0;

    while (tb_now() - start < WATCH_US && !pressed())
    {
        uint32_t due = tb_now() + POLL_US;
        cd_log_t log;

        cd_state(&log);

        for (int i = 0; i < log.count && *count < capacity; i++)
        {
            if (memcmp(log.packets[i].bytes, last, 4) == 0)
                continue;

            memcpy(last, log.packets[i].bytes, 4);
            changes[*count].time = log.start + log.packets[i].time - start;
            memcpy(changes[*count].bytes, last, 4);
            (*count)++;
        }

        if (subq && tb_now() - start >= next_subq && *count < capacity)
        {
            cd_subq_t position;
            int valid = cd_subq(&position);
            changes[*count].time = tb_now() - start;
            changes[*count].bytes[0] = 0x06;
            changes[*count].bytes[1] = (uint8_t)valid;
            changes[*count].bytes[2] = cd_bcd(position.absolute / 4500);
            changes[*count].bytes[3] = cd_bcd(position.absolute / 75 % 60);
            (*count)++;
            next_subq += 1000000u;
        }

        scr_clear_rows(SHELL_BODY_TOP + 5, SHELL_BODY_TOP + 5);
        shell_line(SHELL_BODY_TOP + 5, COLOR_WHITE, "%u S, LAST %02X %02X %02X %02X", (tb_now() - start) / 1000000u,
            last[0], last[1], last[2], last[3]);
        scr_present();

        while ((int32_t)(tb_now() - due) < 0)
            ;
    }
}

// Reads of one sector until one is good or 30 s pass; the time from the call to the good one's data ready
static uint32_t read_until_good(uint32_t* attempts, cd_keep_t* first)
{
    uint32_t start = tb_now();
    cd_log_t log;

    *attempts = 0;

    while (tb_now() - start < READ_LIMIT_US)
    {
        cd_read(&log, CD_READ | CD_STATUS, CD_PAD_LBA, 1);
        (*attempts)++;

        if (first != 0 && *attempts == 1)
            cd_keep(first, &log);

        if (cd_read_ok(&log, 1))
            return log.start + cd_time_of(&log, 0x22, 0) - start;
    }

    return 0;
}

// HT-51 q2: TOC reads every 200 ms until one answers anything but not ready, then 80h; the first try and the answer
// are kept
static void toc_after_change(lid_t* result)
{
    static const uint8_t k_zero[8];
    uint32_t start = tb_now();
    cd_log_t log;

    while (tb_now() - start < READ_LIMIT_US)
    {
        cd_send(&log, CD_TOC | CD_STATUS, k_zero, 0);
        cd_collect(&log, 2000000u, 100000u, 3000000u, 0);
        result->toc_attempts++;

        if (result->toc_attempts == 1)
            cd_keep(&result->toc_first, &log);

        if (!not_ready(&log))
            break;

        tb_delay(200000u);
    }

    cd_keep(&result->toc_answer, &log);
    result->toc_total = log.total;
    cd_state(&log);
    cd_keep(&result->state_after, &log);
}

static int not_ready(const cd_log_t* log)
{
    if (log->count == 0)
        return 1;

    const uint8_t* bytes = log->packets[0].bytes;
    return (bytes[0] == 0x21 || bytes[0] == 0x00) && bytes[1] == 0x09;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 5;
}

static void page(const void* data, uint32_t steps, int number)
{
    const lid_t* result = (const lid_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0 || number == 3)
    {
        const cd_packet_t* changes = number == 0 ? result->changes : result->cdda;
        int count = number == 0 ? result->change_count : result->cdda_count;

        shell_line(row++, COLOR_CYAN, "%s, CHANGES, MS", number == 0 ? "LID OPEN AND CLOSED" : "LID OPEN IN CD-DA");
        row = cd_draw_packets(row, changes, count);

        if (number == 3)
            shell_line(row++, COLOR_GRAY, "06 V MM SS: SUB-Q VALID, POSITION");

        shell_line(row++, COLOR_GRAY, "EXPECTED: 0009 NOT READY, 2108 ONCE");
        return;
    }

    if (number == 4)
    {
        char label[32];

        if (result->toc_attempts == 0)
        {
            shell_line(row++, COLOR_GRAY, "TOC AFTER A CHANGE NOT RUN");
            return;
        }

        row = cd_draw_keep(row, "TOC, FIRST TRY", &result->toc_first);

        if (result->toc_attempts > 1)
        {
            fmt_format(label, sizeof(label), "TOC, TRY %u", result->toc_attempts);
            row = cd_draw_keep(row, label, &result->toc_answer);
        }

        row = cd_draw_keep(row, "THEN", &result->state_after);
        shell_line(row++, COLOR_WHITE, "TOC ANSWER %u PACKETS", result->toc_total);
        shell_line(row++, COLOR_GRAY, "2108 INSTEAD, BEFORE THE TOC, NONE?");
        return;
    }

    if (number == 1)
    {
        row = cd_draw_keep(row, "READ, LID OPEN", &result->open_read);
        row = cd_draw_keep(row, "FIRST READ, CLOSED", &result->closed_first);
        shell_line(row++, COLOR_WHITE, "GOOD READ %u MS AFTER A, ATTEMPT %u", result->closed_time / 1000,
            result->closed_attempts);
        return;
    }

    shell_line(row++, COLOR_CYAN, "CLOSED TO GOOD READ, MS FROM A");

    for (int i = 0; i < result->spin_count; i++)
        shell_line(row++, result->spin[i] != 0 ? COLOR_WHITE : COLOR_YELLOW, "%u  %6u MS  %u ATTEMPTS", i + 1,
            result->spin[i] / 1000, result->spin_attempts[i]);

    if (result->spin_count == 0)
        shell_line(row++, COLOR_GRAY, "NOT RUN");
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const lid_t* result = (const lid_t*)data;
    (void)steps;

    fmt_format(line, 34, "%u CHANGES, SPIN-UP %u MS", result->change_count, result->spin[0] / 1000);
    *color = COLOR_WHITE;
}
