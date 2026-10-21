#include "tests.h"

// 3.5 CD-DA stop and CDC status log (docs/12, HT-27): what 84h+STATUS answers while track 7 plays and what the
// next 80h+STATUS answers: 00 0D as a third packet queued with the stop, as the answer to the next 80h only, or
// never. Then the same with audio already stopped and with a seek in place of the first 80h, pause
// and resume, and every command the BIOS uses sent eight times from idle with its packets and their times

#define LOGS_STOP 4
#define LOGS_STOPPED 7
#define LOGS_PAUSE 6
#define COMMANDS 10
#define RUNS 8
#define PLAY_US 2000000u
#define IDLE_US 200000u
#define SEEK_QUIET 1000000u

typedef struct
{
    uint8_t toc_valid;
    uint8_t reserved[3];
    cd_keep_t stop[LOGS_STOP];
    cd_keep_t stopped[LOGS_STOPPED];
    cd_keep_t pause[LOGS_PAUSE];
    cd_series_t commands[COMMANDS];
} stop_t;

typedef struct
{
    uint8_t command;
    const char* name;
} command_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void play_track(void);
static void keep_command(cd_keep_t* keep, uint8_t command, uint32_t lba);
static void step_stop(stop_t* result);
static void step_stopped(stop_t* result);
static void step_pause(stop_t* result);
static void step_commands(stop_t* result);
static void idle(void);
static int answer_0d(const cd_keep_t* keep);
static const char* verdict(const cd_keep_t* stop, const cd_keep_t* next);

static const test_step_t k_steps[] =
{
    { "STOP WHILE PLAYING", 0 },
    { "STOP WHEN STOPPED, SEEK", 0 },
    { "PAUSE AND RESUME", 0 },
    { "BIOS COMMANDS X8", 0 },
    { 0, 0 }
};

static const command_t k_commands[COMMANDS] =
{
    { CD_STATE, "80" }, { CD_SEEK, "00" }, { CD_READ, "02" }, { CD_PLAY, "04" }, { CD_TOC, "05" },
    { CD_SUBQ, "06" }, { CD_SET, "81" }, { CD_STOP, "84" }, { CD_PAUSE, "85" }, { CD_RESUME, "87" }
};

const test_t g_test_stop =
{
    "3.5", "CD-DA STOP AND STATUS", 0, TEST_RUN_ALL, 1, sizeof(stop_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    stop_t* result = (stop_t*)data;
    cd_log_t log;

    cd_quiet();
    result->toc_valid = (uint8_t)cd_load_toc(&log);

    if ((steps & 0x01) != 0)
        step_stop(result);

    if ((steps & 0x02) != 0)
        step_stopped(result);

    if ((steps & 0x04) != 0)
        step_pause(result);

    if ((steps & 0x08) != 0)
        step_commands(result);

    cd_quiet();
}

static void play_track(void)
{
    cd_log_t log;
    cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
    tb_delay(PLAY_US);
}

static void keep_command(cd_keep_t* keep, uint8_t command, uint32_t lba)
{
    uint8_t parameters[8] = { 0 };
    cd_log_t log;

    if (command == (CD_SEEK | CD_STATUS))
        cd_range(parameters, lba, lba);

    cd_send(&log, command, parameters, 0);
    cd_collect(&log, 2000000u, command == (CD_SEEK | CD_STATUS) ? SEEK_QUIET : 100000u, 4000000u, 0);
    cd_keep(keep, &log);
}

static void step_stop(stop_t* result)
{
    shell_step("STOP WHILE PLAYING");
    play_track();
    keep_command(&result->stop[0], CD_STOP | CD_STATUS, 0);

    for (int i = 1; i < LOGS_STOP; i++)
        keep_command(&result->stop[i], CD_STATE | CD_STATUS, 0);
}

static void step_stopped(stop_t* result)
{
    shell_step("STOP WHEN STOPPED");
    idle();
    keep_command(&result->stopped[0], CD_STOP | CD_STATUS, 0);

    for (int i = 1; i < 4; i++)
        keep_command(&result->stopped[i], CD_STATE | CD_STATUS, 0);

    shell_step("STOP, THEN SEEK");
    play_track();
    keep_command(&result->stopped[4], CD_STOP | CD_STATUS, 0);
    keep_command(&result->stopped[5], CD_SEEK | CD_STATUS, CD_PAD_LBA);
    keep_command(&result->stopped[6], CD_STATE | CD_STATUS, 0);
}

static void step_pause(stop_t* result)
{
    shell_step("PAUSE AND RESUME");
    play_track();
    keep_command(&result->pause[0], CD_PAUSE | CD_STATUS, 0);
    keep_command(&result->pause[1], CD_STATE | CD_STATUS, 0);
    tb_delay(1000000u);
    keep_command(&result->pause[2], CD_RESUME | CD_STATUS, 0);
    keep_command(&result->pause[3], CD_STATE | CD_STATUS, 0);
    tb_delay(1000000u);
    keep_command(&result->pause[4], CD_STOP | CD_STATUS, 0);
    keep_command(&result->pause[5], CD_STATE | CD_STATUS, 0);
}

static void step_commands(stop_t* result)
{
    static const uint8_t k_set[8] = { 0x07, 0xFF, 0, 0, 0, 0, 0, 0 };

    for (int i = 0; i < COMMANDS; i++)
    {
        uint8_t command = k_commands[i].command;

        for (int run = 0; run < RUNS; run++)
        {
            uint8_t parameters[8] = { 0 };
            cd_log_t log;
            int failed = 0;

            shell_step("COMMAND %sH %u/%u", k_commands[i].name, run + 1, RUNS);
            idle();

            if (command == CD_READ)
            {
                cd_read(&log, CD_READ | CD_STATUS, CD_PAD_LBA, 1);
                failed = !cd_read_ok(&log, 1);
            }
            else if (command == CD_PLAY)
            {
                cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
                cd_series_add(&result->commands[i], &log, 0);
                cd_stop(&log);
                continue;
            }
            else
            {
                if (command == CD_SEEK)
                    cd_range(parameters, CD_PAD_LBA, CD_PAD_LBA);
                else if (command == CD_SET)
                    memcpy(parameters, k_set, sizeof(parameters));

                cd_send(&log, (uint8_t)(command | CD_STATUS), parameters, 0);
                cd_collect(&log, 2000000u, command == CD_SEEK ? SEEK_QUIET : 100000u, 4000000u, 0);
            }

            cd_series_add(&result->commands[i], &log, failed);
        }
    }
}

// Drive ready and nothing queued for 200 ms
static void idle(void)
{
    uint32_t start = tb_now();
    uint32_t quiet = tb_now();

    while (tb_now() - start < 3000000u)
    {
        if ((cd_master() & CD_MASTER_SRQ) != 0)
        {
            for (int k = 0; k < 4; k++)
                (void)in8(CD_COMMAND);

            quiet = tb_now();
        }

        if ((cd_master() & CD_MASTER_DRY) != 0 && tb_now() - quiet >= IDLE_US)
            break;
    }
}

static int answer_0d(const cd_keep_t* keep)
{
    for (int i = 0; i < keep->count; i++)
    {
        if (keep->packets[i].bytes[0] == 0x00 && keep->packets[i].bytes[1] == 0x0D)
            return 1;
    }

    return 0;
}

static const char* verdict(const cd_keep_t* stop, const cd_keep_t* next)
{
    if (stop->count == 0)
        return "NO ANSWER TO THE STOP";

    if (answer_0d(stop))
        return "00 0D QUEUED THIRD";

    if (answer_0d(next))
        return "00 0D ANSWERS THE NEXT 80H";

    return "NO 00 0D";
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 6;
}

static void page(const void* data, uint32_t steps, int number)
{
    static const char* const k_stopped[LOGS_STOPPED] = { "STOP", "80H", "80H", "80H", "PLAY,STOP", "SEEK", "80H" };
    static const char* const k_pause[LOGS_PAUSE] = { "PAUSE", "80H", "RESUME", "80H", "STOP", "80H" };
    const stop_t* result = (const stop_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_GREEN, "%s", verdict(&result->stop[0], &result->stop[1]));
        row = cd_draw_keep(row, "STOP AT 2 S", &result->stop[0]);

        for (int i = 1; i < LOGS_STOP; i++)
            row = cd_draw_keep(row, "THEN 80H", &result->stop[i]);

        return;
    }

    if (number == 1)
    {
        for (int i = 0; i < 4; i++)
            row = cd_draw_keep(row, k_stopped[i], &result->stopped[i]);

        return;
    }

    if (number == 2)
    {
        for (int i = 4; i < LOGS_STOPPED; i++)
            row = cd_draw_keep(row, k_stopped[i], &result->stopped[i]);

        return;
    }

    if (number == 3)
    {
        for (int i = 0; i < LOGS_PAUSE; i++)
            row = cd_draw_keep(row, k_pause[i], &result->pause[i]);

        return;
    }

    if (number == 4)
    {
        shell_line(row++, COLOR_CYAN, "8 RUNS FROM IDLE, FIRST RUN'S PACKETS");
        shell_line(row++, COLOR_GRAY, "CMD    1    2    3    4     N SAME");

        for (int i = 0; i < COMMANDS; i++)
        {
            const cd_series_t* series = &result->commands[i];
            row = cd_draw_brief(row, k_commands[i].name, &series->first);
            scr_printf(SCREEN_LEFT + 34, row - 1, series->same == series->runs ? COLOR_GREEN : COLOR_YELLOW, "%u",
                series->same);
        }

        return;
    }

    shell_line(row++, COLOR_CYAN, "FIRST AND LAST PACKET, MS");
    shell_line(row++, COLOR_GRAY, "CMD   FIRST MIN-MAX   LAST MIN-MAX");

    for (int i = 0; i < COMMANDS; i++)
    {
        const cd_series_t* series = &result->commands[i];

        if (series->first_min == 0xFFFFFFFFu)
        {
            shell_line(row++, COLOR_GRAY, "%-4s  NO PACKET", k_commands[i].name);
            continue;
        }

        char text[2][16];
        fmt_format(text[0], sizeof(text[0]), "%u.%u-%u.%u", series->first_min / 1000, series->first_min / 100 % 10,
            series->first_max / 1000, series->first_max / 100 % 10);
        fmt_format(text[1], sizeof(text[1]), "%u.%u-%u.%u", series->last_min / 1000, series->last_min / 100 % 10,
            series->last_max / 1000, series->last_max / 100 % 10);
        shell_line(row++, COLOR_WHITE, "%-4s  %-15s %s", k_commands[i].name, text[0], text[1]);
    }

}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const stop_t* result = (const stop_t*)data;
    (void)steps;

    fmt_format(line, 34, "%s", verdict(&result->stop[0], &result->stop[1]));
    *color = COLOR_WHITE;
}
