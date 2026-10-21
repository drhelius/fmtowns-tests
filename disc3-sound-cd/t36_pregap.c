#include "tests.h"

// 3.6 CD-DA pregaps and emphasis (docs/12, HT-43): the TOC as the CDC reports it, sub-Q every 100 ms while playing
// from track 4's INDEX 00 (440 Hz in the pregap, 880 Hz after it) and from its INDEX 01, and from 3 s before the end
// of the data track into track 2. Whether the pregap tones and the data area are heard, and whether track 5's
// pre-emphasis flag is applied, come from a recording; tracks 5 and 6 differ only in that flag

#define TOC_PACKETS 24
#define SAMPLES 60
#define RUNS 3
#define SAMPLE_US 100000u
#define PREGAP_FRAMES 150
#define DATA_LEAD 225
#define SHOWN 12

enum
{
    RUN_INDEX0,
    RUN_INDEX1,
    RUN_DATA
};

typedef struct
{
    uint16_t time;
    uint8_t status;
    uint8_t valid;
    uint8_t q18[4];
    uint8_t relative[3];
    uint8_t absolute[3];
    uint8_t reserved[2];
} sample_t;

typedef struct
{
    uint32_t first;
    uint32_t end;
    uint8_t count;
    uint8_t reserved[3];
    cd_keep_t play;
    sample_t samples[SAMPLES];
} pregap_run_t;

typedef struct
{
    uint8_t toc_count;
    uint8_t toc_flags;
    uint8_t toc_valid;
    uint8_t reserved;
    cd_packet_t toc[TOC_PACKETS];
    pregap_run_t runs[RUNS];
    uint32_t starts[REC_MAX_STEPS];
} pregap_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void sample_run(pregap_run_t* run, uint32_t first, uint32_t end, uint32_t microseconds);
static void action(int id, int argument, int on);
static int draw_run(int row, const char* title, const pregap_run_t* run);
static int counts_down(const pregap_run_t* run);

static const test_step_t k_steps[] =
{
    { "TOC", 0 },
    { "AUDIO PREGAP, INDEX 01", 0 },
    { "DATA INTO TRACK 2", 0 },
    { "EMPHASIS, TRACKS 5 6", TEST_PHOTO },
    { 0, 0 }
};

static const rec_step_t k_schedule[] =
{
    { "TRACK 5, EMPHASIS", 16000, 1000, 0, 5 },
    { "TRACK 6, NO FLAG", 16000, 1000, 0, 6 }
};

const test_t g_test_pregap =
{
    "3.6", "CD-DA PREGAP, EMPHASIS", "REC", TEST_RUN_ALL, 1, sizeof(pregap_t), k_steps, run, pages, page,
    summary
};

static void run(void* data, uint32_t steps)
{
    pregap_t* result = (pregap_t*)data;
    cd_log_t log;

    cd_quiet();
    shell_step("TOC");
    result->toc_valid = (uint8_t)cd_load_toc(&log);
    result->toc_count = log.count;
    result->toc_flags = log.flags;
    memcpy(result->toc, log.packets, sizeof(result->toc));

    if ((steps & 0x02) != 0)
    {
        shell_step("TRACK 4 FROM INDEX 00");
        sample_run(&result->runs[RUN_INDEX0], cd_track(4) - PREGAP_FRAMES, cd_track(4) + 750, 5000000u);
        cd_quiet();
        shell_step("TRACK 4 FROM INDEX 01");
        sample_run(&result->runs[RUN_INDEX1], cd_track(4), cd_track(4) + 750, 5000000u);
        cd_quiet();
    }

    if ((steps & 0x04) != 0)
    {
        shell_step("DATA TRACK INTO TRACK 2");
        sample_run(&result->runs[RUN_DATA], CD_DATA_SECTORS - DATA_LEAD, cd_track(2) + 375, 6000000u);
        cd_quiet();
    }

    if ((steps & 0x08) != 0)
    {
        snd_saved_t saved;
        snd_open(&saved);
        shell_step("EMPHASIS");
        rec_play("PRE-EMPHASIS FLAG", k_schedule, COUNT_OF(k_schedule), action, result->starts);
        snd_close(&saved);
    }

    cd_quiet();
}

static void sample_run(pregap_run_t* run, uint32_t first, uint32_t end, uint32_t microseconds)
{
    cd_log_t log;

    run->first = first;
    run->end = end;
    cd_play(&log, first, end, 0);
    cd_keep(&run->play, &log);
    uint32_t start = tb_now();

    for (int i = 0; i < SAMPLES; i++)
    {
        uint32_t due = (uint32_t)i * SAMPLE_US;
        cd_subq_t subq;

        if (due >= microseconds)
            break;

        while (tb_now() - start < due)
            ;

        sample_t* sample = &run->samples[i];
        sample->time = (uint16_t)((tb_now() - start) / 1000u);
        sample->valid = (uint8_t)cd_subq(&subq);
        sample->status = subq.status;
        memcpy(sample->q18, subq.raw, 4);
        memcpy(sample->relative, subq.raw + 5, 3);
        sample->absolute[0] = subq.raw[10];
        sample->absolute[1] = subq.raw[11];
        sample->absolute[2] = subq.raw[13];
        run->count = (uint8_t)(i + 1);
    }
}

static void action(int id, int argument, int on)
{
    cd_log_t log;
    (void)id;

    if (on)
        cd_play(&log, cd_track(argument), cd_track_end(argument) + 1, 0);
    else
        cd_stop(&log);
}

// The relative time falls while the pickup is in a pregap, as on a Red Book player
static int counts_down(const pregap_run_t* run)
{
    for (int i = 1; i < run->count; i++)
    {
        const sample_t* a = &run->samples[i - 1];
        const sample_t* b = &run->samples[i];
        uint32_t before = cd_from_bcd(a->relative[0]) * 4500 + cd_from_bcd(a->relative[1]) * 75 +
            cd_from_bcd(a->relative[2]);
        uint32_t after = cd_from_bcd(b->relative[0]) * 4500 + cd_from_bcd(b->relative[1]) * 75 +
            cd_from_bcd(b->relative[2]);

        if (a->valid && b->valid && after < before)
            return 1;
    }

    return 0;
}

static int draw_run(int row, const char* title, const pregap_run_t* run)
{
    uint8_t first[3];
    uint8_t end[3];
    int step = run->count > SHOWN ? (run->count + SHOWN - 1) / SHOWN : 1;

    if (run->count == 0 && run->play.count == 0)
    {
        shell_line(row++, COLOR_CYAN, "%s", title);
        shell_line(row++, COLOR_GRAY, "NOT RUN");
        return row;
    }

    cd_msf(run->first, first);
    cd_msf(run->end, end);
    shell_line(row++, COLOR_CYAN, "%s %02X:%02X:%02X-%02X:%02X:%02X", title, first[0], first[1], first[2], end[0],
        end[1], end[2]);

    if (run->play.count != 0)
        shell_line(row++, COLOR_WHITE, "PLAY %02X%02X %u.%u MS %s", run->play.packets[0].bytes[0],
            run->play.packets[0].bytes[1], run->play.packets[0].time / 1000, run->play.packets[0].time / 100 % 10,
            cd_flags_name(run->play.flags));
    else
        shell_line(row++, COLOR_YELLOW, "PLAY: NO ANSWER");

    shell_line(row++, COLOR_GRAY, "  MS ST 18H PACKET RELATIVE ABSOLUTE");

    for (int i = 0; i < run->count && row <= SHELL_BODY_BOTTOM - 1; i += step)
    {
        const sample_t* sample = &run->samples[i];

        if (!sample->valid)
        {
            shell_line(row++, COLOR_YELLOW, "%4u %02X NO SUB-Q", sample->time, sample->status);
            continue;
        }

        shell_line(row++, COLOR_WHITE, "%4u %02X %02X%02X%02X%02X %02X:%02X:%02X %02X:%02X:%02X", sample->time,
            sample->status, sample->q18[0], sample->q18[1], sample->q18[2], sample->q18[3], sample->relative[0],
            sample->relative[1], sample->relative[2], sample->absolute[0], sample->absolute[1], sample->absolute[2]);
    }

    shell_line(row++, counts_down(run) ? COLOR_GREEN : COLOR_WHITE, "RELATIVE %s",
        counts_down(run) ? "COUNTS DOWN (RED BOOK)" : "NEVER FALLS");
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
    const pregap_t* result = (const pregap_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "TOC: %u PACKETS, %s %s", result->toc_count,
            result->toc_valid ? "ALL TRACKS" : "INCOMPLETE", cd_flags_name(result->toc_flags));
        row = cd_draw_packets(row, result->toc, result->toc_count);
        row++;
        shell_line(row++, COLOR_GRAY, "BUILT: T2 08:57:25 T4 09:47:25 PRE T5");
        shell_line(row++, COLOR_GRAY, "T4 INDEX 00 09:45:25, OUT 11:27:25");
        return;
    }

    if (number == 4)
    {
        row = rec_draw(row, "EMPHASIS, RECORDED", k_schedule, COUNT_OF(k_schedule), result->starts);
        row++;
        shell_line(row++, COLOR_WHITE, "10 DB LESS AT 16 KHZ IN TRACK 5:");
        shell_line(row++, COLOR_WHITE, "DE-EMPHASIS APPLIED");
        shell_line(row++, COLOR_WHITE, "TRACKS ALIKE: FLAG IGNORED");
        return;
    }

    static const char* const k_titles[RUNS] = { "T4 INDEX 00", "T4 INDEX 01", "DATA END" };
    row = draw_run(row, k_titles[number - 1], &result->runs[number - 1]);

    if (number == 1)
        shell_line(row++, COLOR_GRAY, "440 HZ HEARD: THE PREGAP PLAYS");
    else if (number == 3)
        shell_line(row++, COLOR_GRAY, "DATA AREA: SILENT OR NOISE");
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const pregap_t* result = (const pregap_t*)data;
    (void)steps;

    fmt_format(line, 34, "TOC %s, PREGAP %s", result->toc_valid ? "OK" : "BAD",
        counts_down(&result->runs[RUN_INDEX0]) ? "COUNTS DOWN" : "NO COUNTDOWN");
    *color = result->toc_valid ? COLOR_WHITE : COLOR_YELLOW;
}
