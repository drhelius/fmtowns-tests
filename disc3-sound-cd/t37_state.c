#include "tests.h"

// 3.7 CDC drive state during reads (docs/12, HT-47): the second byte of the read statuses, which was 05h at the
// acceptance and 02h at data ready and read done in the Marty's first read on disc 1 (1.6). One-sector reads with
// STATUS, every packet logged; 80h polls around them, after idle times, around CD-DA play, pause and stop, and the
// same byte in a seek and a raw read. The disc's first CDC command since boot,
// whichever test sends it, is a read of LBA 1 (cd.c); step 1 shows it

#define EVENTS 128
#define ATTEMPTS 8
#define READS 8
#define POLL_SECONDS 10
#define IDLE_WAITS 5
#define GROUPS 9
#define FIRST_LBA 1

enum
{
    STEP_REPEAT = 1,
    STEP_POLL,
    STEP_IDLE,
    STEP_CDDA,
    STEP_OTHER
};

enum
{
    KIND_READ,
    KIND_STATE,
    KIND_PLAY,
    KIND_STOP,
    KIND_PAUSE,
    KIND_RESUME,
    KIND_SEEK,
    KIND_RAW
};

typedef struct
{
    uint8_t step;
    uint8_t kind;
    uint8_t attempt;
    uint8_t count;
    uint8_t bytes[3][4];
    uint16_t times[3];
    uint8_t flags;
    uint8_t reserved;
} event_t;

typedef struct
{
    uint8_t reserved;
    uint8_t overflow;
    uint16_t count;
    uint32_t first_time;
    uint32_t idle_waits[IDLE_WAITS];
    cd_keep_t first;
    event_t events[EVENTS];
} state_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void add_event(state_t* result, uint8_t step, uint8_t kind, uint8_t attempt, const cd_log_t* log);
static void read_logged(state_t* result, uint8_t step, uint8_t command, uint32_t lba, cd_log_t* log);
static void poll(state_t* result, uint8_t step);
static void simple(state_t* result, uint8_t step, uint8_t kind, uint8_t command);
static void seek(state_t* result, uint8_t step, uint32_t lba);
static void step_repeat(state_t* result, int polls);
static void step_idle(state_t* result);
static void step_cdda(state_t* result);
static void step_other(state_t* result);
static int draw_groups(int row, const state_t* result, uint8_t step);

static const test_step_t k_steps[] =
{
    { "FIRST READ SINCE BOOT", 0 },
    { "BACK TO BACK READS", 0 },
    { "80H AROUND READS, 10 S", 0 },
    { "AFTER IDLE, 230 S", 0 },
    { "AROUND CD-DA", 0 },
    { "SEEK AND RAW READ", 0 },
    { 0, 0 }
};

static const char* const k_kinds[] = { "RD", "80", "PL", "84", "85", "87", "00", "03" };
static const uint32_t k_idle_us[IDLE_WAITS] = { 5000000u, 15000000u, 30000000u, 60000000u, 120000000u };

const test_t g_test_state =
{
    "3.7", "CDC DRIVE STATE", 0, TEST_RUN_ALL, 1, sizeof(state_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    state_t* result = (state_t*)data;
    cd_log_t log;

    cd_quiet();

    if ((steps & 0x01) != 0)
        result->first = *cd_first_read(&result->first_time);

    cd_load_toc(&log);

    if ((steps & 0x06) != 0)
        step_repeat(result, (steps & 0x04) != 0);

    if ((steps & 0x08) != 0)
        step_idle(result);

    if ((steps & 0x10) != 0)
        step_cdda(result);

    if ((steps & 0x20) != 0)
        step_other(result);

    cd_quiet();
}

static void add_event(state_t* result, uint8_t step, uint8_t kind, uint8_t attempt, const cd_log_t* log)
{
    int kept = 0;

    if (result->count >= EVENTS)
    {
        result->overflow = 1;
        return;
    }

    event_t* event = &result->events[result->count++];
    event->step = step;
    event->kind = kind;
    event->attempt = attempt;
    event->flags = log->flags;

    for (int i = 0; i < log->count; i++)
    {
        const cd_packet_t* packet = &log->packets[i];

        if (packet->bytes[0] >= CD_PACKET_DMA_NONE)
            continue;

        event->count++;

        if (kept < 3)
        {
            memcpy(event->bytes[kept], packet->bytes, 4);
            event->times[kept] = (uint16_t)(packet->time / 100u < 0xFFFF ? packet->time / 100u : 0xFFFF);
            kept++;
        }
    }
}

// Up to eight attempts, each one logged; the first read since boot keeps every packet as well
static void read_logged(state_t* result, uint8_t step, uint8_t command, uint32_t lba, cd_log_t* log)
{
    for (int attempt = 1; attempt <= ATTEMPTS; attempt++)
    {
        cd_read(log, command, lba, 1);

        add_event(result, step, (command & 0x1F) == CD_RAW_READ ? KIND_RAW : KIND_READ, (uint8_t)attempt, log);

        if (cd_read_ok(log, 1))
            return;
    }
}

static void poll(state_t* result, uint8_t step)
{
    cd_log_t log;
    cd_state(&log);
    add_event(result, step, KIND_STATE, 1, &log);
}

static void simple(state_t* result, uint8_t step, uint8_t kind, uint8_t command)
{
    static const uint8_t k_zero[8];
    cd_log_t log;
    cd_command(&log, command, k_zero);
    add_event(result, step, kind, 1, &log);
}

static void seek(state_t* result, uint8_t step, uint32_t lba)
{
    uint8_t parameters[8];
    cd_log_t log;

    cd_range(parameters, lba, lba);
    cd_send(&log, CD_SEEK | CD_STATUS, parameters, 0);
    cd_collect(&log, 2000000u, 1000000u, 4000000u, CD_UNTIL_SEEK);
    add_event(result, step, KIND_SEEK, 1, &log);
}

// Eight reads of one sector, eight in order, eight alternating with the last data sector; 80h around each
static void step_repeat(state_t* result, int polls)
{
    cd_log_t log;

    for (int set = 0; set < 3; set++)
    {
        for (int i = 0; i < READS; i++)
        {
            uint32_t lba = set == 0 ? CD_PAD_LBA : set == 1 ? CD_PAD_LBA + 1 + (uint32_t)i :
                (i & 1) != 0 ? CD_DATA_SECTORS - 1 : CD_PAD_LBA;

            shell_step("READS %u/%u LBA %u", set * READS + i + 1, 3 * READS, lba);

            if (polls)
                poll(result, STEP_POLL);

            read_logged(result, STEP_REPEAT, CD_READ | CD_STATUS, lba, &log);

            if (polls)
                poll(result, STEP_POLL);
        }
    }

    if (!polls)
        return;

    uint32_t start = tb_now();

    for (int i = 1; i <= POLL_SECONDS; i++)
    {
        shell_step("80H EVERY SECOND %u/%u", i, POLL_SECONDS);

        while (tb_now() - start < (uint32_t)i * 1000000u)
            ;

        poll(result, STEP_POLL);
    }
}

// Nothing sent for each wait, then 80h, then a read
static void step_idle(state_t* result)
{
    cd_log_t log;

    for (int i = 0; i < IDLE_WAITS; i++)
    {
        uint32_t start = tb_now();
        uint32_t shown = 0xFFFFFFFFu;

        while (tb_now() - start < k_idle_us[i])
        {
            uint32_t left = (k_idle_us[i] - (tb_now() - start)) / 1000000u;

            if (left != shown)
            {
                shown = left;
                shell_step("IDLE %u S, %u LEFT", k_idle_us[i] / 1000000u, left);
            }
        }

        result->idle_waits[i] = tb_now() - start;
        poll(result, STEP_IDLE);
        read_logged(result, STEP_IDLE, CD_READ | CD_STATUS, CD_PAD_LBA, &log);
    }
}

static void step_cdda(state_t* result)
{
    cd_log_t log;

    shell_step("PLAY, READ");
    cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
    add_event(result, STEP_CDDA, KIND_PLAY, 1, &log);
    tb_delay(2000000u);
    read_logged(result, STEP_CDDA, CD_READ | CD_STATUS, CD_PAD_LBA, &log);
    poll(result, STEP_CDDA);

    shell_step("PLAY, STOP, READ");
    cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
    add_event(result, STEP_CDDA, KIND_PLAY, 1, &log);
    tb_delay(2000000u);
    simple(result, STEP_CDDA, KIND_STOP, CD_STOP | CD_STATUS);
    read_logged(result, STEP_CDDA, CD_READ | CD_STATUS, CD_PAD_LBA, &log);

    shell_step("PLAY, PAUSE, READ, RESUME");
    cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
    add_event(result, STEP_CDDA, KIND_PLAY, 1, &log);
    tb_delay(2000000u);
    simple(result, STEP_CDDA, KIND_PAUSE, CD_PAUSE | CD_STATUS);
    read_logged(result, STEP_CDDA, CD_READ | CD_STATUS, CD_PAD_LBA, &log);
    simple(result, STEP_CDDA, KIND_RESUME, CD_RESUME | CD_STATUS);
    poll(result, STEP_CDDA);
    tb_delay(1000000u);
    poll(result, STEP_CDDA);
    simple(result, STEP_CDDA, KIND_STOP, CD_STOP | CD_STATUS);
}

static void step_other(state_t* result)
{
    cd_log_t log;

    shell_step("SEEK FROM IDLE");
    tb_delay(2000000u);
    seek(result, STEP_OTHER, CD_PAD_LBA + 100);
    poll(result, STEP_OTHER);
    shell_step("RAW READ FROM IDLE");
    tb_delay(2000000u);
    read_logged(result, STEP_OTHER, CD_RAW_READ | CD_STATUS, CD_PAD_LBA + 200, &log);
    poll(result, STEP_OTHER);
    shell_step("SEEK AFTER A READ");
    read_logged(result, STEP_OTHER, CD_READ | CD_STATUS, CD_PAD_LBA, &log);
    seek(result, STEP_OTHER, CD_PAD_LBA + 100);
    shell_step("RAW READ AFTER A READ");
    read_logged(result, STEP_OTHER, CD_READ | CD_STATUS, CD_PAD_LBA, &log);
    read_logged(result, STEP_OTHER, CD_RAW_READ | CD_STATUS, CD_PAD_LBA + 200, &log);
}

// Each distinct sequence of a step once, in order of first appearance, with how many times it came
static int draw_groups(int row, const state_t* result, uint8_t step)
{
    int shown = 0;

    for (int i = 0; i < result->count; i++)
    {
        const event_t* event = &result->events[i];
        int seen = 0;
        int times = 0;

        if (event->step != step)
            continue;

        for (int k = 0; k < result->count; k++)
        {
            const event_t* other = &result->events[k];
            int same = other->step == step && other->kind == event->kind && other->count == event->count;

            for (int p = 0; same && p < 3 && p < event->count; p++)
                same = other->bytes[p][0] == event->bytes[p][0] && other->bytes[p][1] == event->bytes[p][1];

            if (!same)
                continue;

            if (k < i)
                seen = 1;

            times++;
        }

        if (seen)
            continue;

        if (shown++ >= GROUPS || row > SHELL_BODY_BOTTOM)
            break;

        char text[3][6];

        for (int p = 0; p < 3; p++)
        {
            text[p][0] = 0;

            if (p < event->count)
                fmt_format(text[p], sizeof(text[p]), "%02X%02X", event->bytes[p][0], event->bytes[p][1]);
        }

        shell_line(row++, COLOR_WHITE, "%s %-4s %-4s %-4s%s X%-2u %5u.%u", k_kinds[event->kind], text[0], text[1],
            text[2], event->count > 3 ? "+" : " ", times, event->times[0] / 10,
            event->times[0] % 10);
    }

    if (shown == 0)
        shell_line(row++, COLOR_GRAY, "NOT RUN");

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
    const state_t* result = (const state_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "FIRST COMMAND SINCE BOOT, AT %u MS:", result->first_time / 1000);
        shell_line(row++, COLOR_CYAN, "A READ OF LBA %u", FIRST_LBA);
        row = cd_draw_keep(row, "PACKETS, MS", &result->first);
        shell_line(row++, COLOR_GRAY, "1.6 ON THE MARTY: 0005 2202 0602");
        row++;
        shell_line(row++, COLOR_CYAN, "SEQUENCES, FIRST PACKET MS");
        row = draw_groups(row, result, STEP_REPEAT);

        if (result->overflow)
            shell_line(row++, COLOR_YELLOW, "EVENT LOG FULL");

        return;
    }

    if (number == 1)
    {
        shell_line(row++, COLOR_CYAN, "80H AROUND READS, THEN EVERY SECOND");
        row = draw_groups(row, result, STEP_POLL);
        row++;
        shell_line(row++, COLOR_CYAN, "AFTER 5 15 30 60 120 S IDLE");
        row = draw_groups(row, result, STEP_IDLE);
        return;
    }

    if (number == 2)
    {
        shell_line(row++, COLOR_CYAN, "AROUND CD-DA, IN ORDER");

        for (int i = 0; i < result->count && row <= SHELL_BODY_BOTTOM; i++)
        {
            const event_t* event = &result->events[i];

            if (event->step != STEP_CDDA)
                continue;

            shell_line(row++, COLOR_WHITE, "%s %02X%02X %02X%02X %02X%02X %c N%u %5u.%u", k_kinds[event->kind],
                event->bytes[0][0], event->bytes[0][1], event->bytes[1][0], event->bytes[1][1], event->bytes[2][0],
                event->bytes[2][1], event->attempt > 1 ? 'R' : ' ', event->count, event->times[0] / 10,
                event->times[0] % 10);
        }

        return;
    }

    shell_line(row++, COLOR_CYAN, "SEEK AND RAW READ, IN ORDER");

    for (int i = 0; i < result->count && row <= SHELL_BODY_BOTTOM; i++)
    {
        const event_t* event = &result->events[i];

        if (event->step != STEP_OTHER)
            continue;

        shell_line(row++, COLOR_WHITE, "%s %02X%02X %02X%02X %02X%02X %c N%u %5u.%u", k_kinds[event->kind],
            event->bytes[0][0], event->bytes[0][1], event->bytes[1][0], event->bytes[1][1], event->bytes[2][0],
            event->bytes[2][1], event->attempt > 1 ? 'R' : ' ', event->count, event->times[0] / 10,
            event->times[0] % 10);
    }

}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const state_t* result = (const state_t*)data;
    int failed = 0;
    (void)steps;

    for (int i = 0; i < result->count; i++)
        failed += result->events[i].attempt > 1;

    fmt_format(line, 34, "FIRST %02X%02X %02X%02X RETRIES %u", result->first.packets[0].bytes[0],
        result->first.packets[0].bytes[1], result->first.packets[1].bytes[0], result->first.packets[1].bytes[1],
        failed);
    *color = failed != 0 ? COLOR_YELLOW : COLOR_WHITE;
}
