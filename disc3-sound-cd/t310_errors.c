#include "tests.h"

// 3.10 CDC errors and edge cases (docs/12, HT-50): reads outside the data, malformed and extra parameters, commands
// without STATUS, every command byte the BIOS doesn't use, SRST at four points, a command sent while a read delivers,
// the CDC ports read with nothing queued, and 80h written while a TOC reply is still queued. Each case logs its
// packets; the drive is checked with a good read before and after the cases that could upset it

#define OUTSIDE 5
#define MALFORMED 6
#define PLAIN 11
#define UNKNOWN 52
#define SRST_CASES 4
#define DURING 3
#define IDLE_PORTS 8
#define UNKNOWN_QUIET 100000u
#define LEFTOVERS 3
#define LONE_RUNS 8
#define NO_REPLY 0xFF

typedef struct
{
    uint8_t command;
    uint8_t count;
    uint8_t flags;
    uint8_t bytes[2][2];
    uint8_t reserved;
    uint16_t time;
} unknown_t;

typedef struct
{
    cd_keep_t log;
    uint8_t master[2];
    uint8_t irq[2];
    uint32_t before;
    uint32_t after;
} srst_t;

typedef struct
{
    cd_keep_t log;
    uint8_t first[4];
    uint8_t master;
    uint8_t old;
    uint8_t reply;
    uint8_t total;
    uint32_t write;
    uint32_t reply_time;
} leftover_t;

typedef struct
{
    cd_keep_t outside[OUTSIDE];
    uint8_t good_before[OUTSIDE];
    uint8_t good_after[OUTSIDE];
    uint8_t reserved[2];
    cd_keep_t malformed[MALFORMED];
    uint32_t malformed_lba[MALFORMED];
    cd_keep_t plain[PLAIN];
    unknown_t unknown[UNKNOWN];
    cd_keep_t special[2];
    srst_t srst[SRST_CASES];
    cd_keep_t during[DURING];
    uint16_t during_ready[DURING];
    uint8_t idle_ports[IDLE_PORTS];
    uint8_t resets;
    uint8_t reserved2[3];
    leftover_t leftovers[LEFTOVERS];
    uint32_t lone_min;
    uint32_t lone_max;
    uint8_t lone_runs;
    uint8_t reserved3[3];
} errors_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static uint8_t good_read(void);
static void recover(errors_t* result);
static void step_outside(errors_t* result);
static void step_malformed(errors_t* result);
static void step_plain(errors_t* result);
static void step_pending(errors_t* result);
static void step_unknown(errors_t* result);
static void step_srst(errors_t* result, int dma);
static void step_during(errors_t* result);
static void step_idle(errors_t* result);
static void step_leftover(errors_t* result);
static void leftover(leftover_t* entry, uint32_t delay);
static const char* leftover_verdict(const leftover_t* entry);
static void raw_read(cd_log_t* log, const uint8_t* parameters, int count, uint32_t lba);

static const test_step_t k_steps[] =
{
    { "READS OUTSIDE THE DATA", 0 },
    { "BAD AND EXTRA PARAMETERS", 0 },
    { "COMMANDS WITHOUT STATUS", 0 },
    { "UNKNOWN COMMANDS", 0 },
    { "SRST", 0 },
    { "COMMAND DURING A READ", 0 },
    { "IDLE PORT READS", 0 },
    { "LEFTOVER PACKETS", 0 },
    { "SRST DURING A DMA", TEST_RISKY },
    { 0, 0 }
};

static const char* const k_outside[OUTSIDE] = { "AUDIO SECTOR", "TRACK 2 PREGAP", "DATA END + 1", "DISC END + 1",
    "START AFTER END" };
static const char* const k_malformed[MALFORMED] = { "FRAME DIGIT 0AH", "FRAME 75", "00:00:00", "10 PARAMETERS",
    "6, THEN 2 MORE", "TOC, NONE, THEN 8" };
static const uint8_t k_plain[PLAIN - 1] = { CD_STATE, CD_SEEK, CD_READ, CD_PLAY, CD_TOC, CD_SUBQ, CD_SET, CD_STOP,
    CD_PAUSE, CD_RESUME };
static const char* const k_srst[SRST_CASES] = { "IDLE", "BETWEEN SECTORS", "DURING A DMA", "DURING CD-DA" };
static const char* const k_during[DURING] = { "80H", "00H TO 5000", "02H OF 3000" };
static const uint16_t k_ports[IDLE_PORTS] = { 0x04C0, 0x04C2, 0x04C4, 0x04C6, 0x04C8, 0x04CA, 0x04CC, 0x04CD };
static const uint32_t k_leftover_us[LEFTOVERS] = { 10, 500, 2000 };

const test_t g_test_errors =
{
    "3.10", "CDC ERRORS, EDGE CASES", 0, TEST_RUN_ALL, 3, sizeof(errors_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    errors_t* result = (errors_t*)data;
    cd_log_t log;

    cd_quiet();
    cd_load_toc(&log);

    if ((steps & 0x40) != 0)
        step_idle(result);

    if ((steps & 0x01) != 0)
        step_outside(result);

    if ((steps & 0x02) != 0)
        step_malformed(result);

    if ((steps & 0x04) != 0)
        step_plain(result);

    if ((steps & 0x08) != 0)
        step_unknown(result);

    if ((steps & 0x10) != 0)
        step_srst(result, 0);

    if ((steps & 0x20) != 0)
        step_during(result);

    if ((steps & 0x80) != 0)
        step_leftover(result);

    if ((steps & 0x100) != 0)
        step_srst(result, 1);

    cd_masks(0);
    cd_quiet();
}

// The attempt that worked, up to four, or 0
static uint8_t good_read(void)
{
    cd_log_t log;
    return (uint8_t)cd_read_retry(&log, CD_READ | CD_STATUS, CD_PAD_LBA, 1, 4);
}

// A CDC that stays busy gets an SRST
static void recover(errors_t* result)
{
    uint32_t start = tb_now();

    out8(0x00AF, 0x0F);

    while ((cd_master() & CD_MASTER_DRY) == 0 && tb_now() - start < 2000000u)
        ;

    if ((cd_master() & CD_MASTER_DRY) == 0)
    {
        cd_reset();
        tb_delay(10000);
        result->resets++;
    }

    cd_quiet();
}

static void raw_read(cd_log_t* log, const uint8_t* parameters, int count, uint32_t lba)
{
    cd_arm(CD_READ, 1);
    cd_send_raw(log, CD_READ | CD_STATUS, parameters, count, 0);
    log->lba = lba;
    cd_collect(log, 3000000u, 4000000u, 9000000u, CD_DMA | CD_UNTIL_END);
}

static void step_outside(errors_t* result)
{
    for (int i = 0; i < OUTSIDE; i++)
    {
        uint8_t parameters[8];
        cd_log_t log;
        uint32_t leadout = g_cd_toc.leadout;

        shell_step("OUTSIDE: %s", k_outside[i]);
        result->good_before[i] = good_read();

        if (i == 0)
            cd_range(parameters, cd_track(2), cd_track(2));
        else if (i == 1)
            cd_range(parameters, CD_DATA_SECTORS + 75, CD_DATA_SECTORS + 75);
        else if (i == 2)
            cd_range(parameters, CD_DATA_SECTORS, CD_DATA_SECTORS);
        else if (i == 3)
            cd_range(parameters, leadout, leadout);
        else
            cd_range(parameters, CD_PAD_LBA + 10, CD_PAD_LBA);

        raw_read(&log, parameters, 8, CD_NO_LBA);
        cd_keep(&result->outside[i], &log);
        recover(result);
        result->good_after[i] = good_read();
    }
}

static void step_malformed(errors_t* result)
{
    for (int i = 0; i < MALFORMED; i++)
    {
        uint8_t parameters[10];
        cd_log_t log;

        shell_step("PARAMETERS: %s", k_malformed[i]);
        memset(parameters, 0, sizeof(parameters));

        if (i == 0)
        {
            cd_range(parameters, CD_PAD_LBA, CD_PAD_LBA);
            parameters[2] = 0x0A;
            parameters[5] = 0x0A;
            raw_read(&log, parameters, 8, CD_NO_LBA);
        }
        else if (i == 1)
        {
            cd_range(parameters, CD_PAD_LBA, CD_PAD_LBA);
            parameters[2] = 0x75;
            parameters[5] = 0x75;
            raw_read(&log, parameters, 8, CD_NO_LBA);
        }
        else if (i == 2)
            raw_read(&log, parameters, 8, CD_NO_LBA);
        else if (i == 3)
        {
            parameters[0] = 0x99;
            parameters[1] = 0x99;
            cd_range(parameters + 2, CD_PAD_LBA, CD_PAD_LBA);
            raw_read(&log, parameters, 10, CD_PAD_LBA);
        }
        else if (i == 5)
        {
            cd_send_raw(&log, CD_TOC | CD_STATUS, parameters, 0, 0);
            cd_poll(&log, 1000000u);

            for (int k = 0; k < 8; k++)
                out8(CD_PARAMETER, 0);

            cd_collect(&log, 1000000u, 200000u, 3000000u, 0);
        }
        else
        {
            cd_range(parameters, CD_PAD_LBA, CD_PAD_LBA);
            cd_arm(CD_READ, 1);
            cd_send_raw(&log, CD_READ | CD_STATUS, parameters, 6, 0);
            log.lba = CD_PAD_LBA;
            cd_poll(&log, 500000u);
            out8(CD_PARAMETER, 0);
            out8(CD_PARAMETER, 0);
            cd_collect(&log, 3000000u, 1000000u, 5000000u, CD_DMA | CD_UNTIL_END);
        }

        result->malformed_lba[i] = cd_check_sector(CD_BUFFER, CD_PAD_LBA, 0);
        cd_keep(&result->malformed[i], &log);
        recover(result);
    }
}

// The commands of 3.5 step 4 without STATUS: which packets come anyway
static void step_plain(errors_t* result)
{
    static const uint8_t k_set[8] = { 0x07, 0xFF, 0, 0, 0, 0, 0, 0 };

    for (int i = 0; i < PLAIN - 1; i++)
    {
        uint8_t parameters[8] = { 0 };
        cd_log_t log;
        uint8_t command = k_plain[i];

        shell_step("NO STATUS: %02XH", command);

        if (command == CD_SEEK || command == CD_READ)
            cd_range(parameters, CD_PAD_LBA, CD_PAD_LBA);
        else if (command == CD_PLAY)
        {
            cd_msf(cd_track(7), parameters);
            cd_msf(cd_track_end(7) + 1, parameters + 3);
        }
        else if (command == CD_SET)
            memcpy(parameters, k_set, sizeof(parameters));

        if (command == CD_PAUSE || command == CD_RESUME || command == CD_STOP)
        {
            cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
            tb_delay(1000000u);
        }

        cd_arm(CD_READ, 1);
        cd_send(&log, command, parameters, 0);
        log.lba = CD_PAD_LBA;
        cd_collect(&log, 1000000u, command == CD_SEEK ? 1000000u : 200000u, 3000000u, CD_DMA);
        out8(0x00AF, 0x0F);
        cd_keep(&result->plain[i], &log);
        recover(result);
    }

    step_pending(result);
}

// 80h with STATUS and IRQ raises SIRQ; its packet is read but SIRQ not acknowledged, then a seek goes out without
// STATUS or IRQ. Fractal Engine Demo still waits for 00 00 and 04 00, as if it inherited both
static void step_pending(errors_t* result)
{
    static const uint8_t k_zero[8];
    uint8_t parameters[8];
    cd_log_t log;

    shell_step("SEEK WITH SIRQ PENDING");
    cd_masks(CD_CONTROL_SMIM);
    cd_send(&log, CD_STATE | CD_STATUS | CD_IRQ, k_zero, 0);
    cd_collect(&log, 1000000u, 50000u, 2000000u, 0);
    cd_range(parameters, CD_PAD_LBA + 300, CD_PAD_LBA + 300);
    cd_begin(&log, CD_SEEK);

    for (int k = 0; k < 8; k++)
        out8(CD_PARAMETER, parameters[k]);

    out8(CD_COMMAND, CD_SEEK);
    cd_collect(&log, 1500000u, 1500000u, 3000000u, 0);
    cd_keep(&result->plain[PLAIN - 1], &log);
    cd_masks(0);
    recover(result);
}

// Each command byte from 00h-1Fh and 80h-9Fh that no BIOS routine sends, with STATUS and a one-sector range
static void step_unknown(errors_t* result)
{
    static const uint8_t k_known[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x80, 0x81, 0x84, 0x85, 0x87 };
    static const uint8_t k_query[8] = { 0x03, 0, 0, 0, 0, 0, 0, 0 };
    static const uint8_t k_windows[8] = { 0x00, 0x5F, 0xFC, 0x5F, 0xFC, 0, 0, 0 };
    int index = 0;

    for (int code = 0; code < 0x40 && index < UNKNOWN; code++)
    {
        uint8_t command = (uint8_t)(code < 0x20 ? code : 0x80 + code - 0x20);
        int known = 0;
        uint8_t parameters[8];
        cd_log_t log;

        for (unsigned k = 0; k < COUNT_OF(k_known); k++)
            known |= k_known[k] == command;

        if (known)
            continue;

        shell_step("UNKNOWN %02XH", command | CD_STATUS);
        cd_range(parameters, CD_PAD_LBA, CD_PAD_LBA);
        cdc_dma_setup(3, CD_BUFFER, 4095, 0x54);
        cd_send(&log, (uint8_t)(command | CD_STATUS), parameters, 0);
        log.lba = CD_PAD_LBA;
        cd_collect(&log, UNKNOWN_QUIET, UNKNOWN_QUIET, 2000000u, CD_DMA);

        unknown_t* entry = &result->unknown[index++];
        entry->command = (uint8_t)(command | CD_STATUS);
        entry->count = log.total;
        entry->flags = log.flags;

        for (int p = 0; p < 2 && p < log.count; p++)
        {
            entry->bytes[p][0] = log.packets[p].bytes[0];
            entry->bytes[p][1] = log.packets[p].bytes[1];
        }

        if (log.count != 0)
            entry->time = (uint16_t)(log.packets[0].time / 100u < 0xFFFF ? log.packets[0].time / 100u : 0xFFFF);

        recover(result);
    }

    for (int i = 0; i < 2; i++)
    {
        cd_log_t log;
        shell_step("%s", i == 0 ? "3FH WITH P0 = 3" : "BFH 5F FC 5F FC");
        cd_command(&log, i == 0 ? 0x3F : 0xBF, i == 0 ? k_query : k_windows);
        cd_keep(&result->special[i], &log);
        recover(result);
    }
}

// 04C0h and IRQ9 right after the reset and 1 ms later, then every packet in 500 ms; both masks set. The reset during
// a DMA is a step of its own: it locked the Marty 2
static void step_srst(errors_t* result, int dma)
{
    for (int i = 0; i < SRST_CASES; i++)
    {
        srst_t* entry = &result->srst[i];
        uint8_t parameters[8];
        cd_subq_t subq;
        cd_log_t log;

        if ((i == 2) != dma)
            continue;

        shell_step("SRST %s", k_srst[i]);
        cd_masks(CD_CONTROL_SMIM | CD_CONTROL_DEIM);

        if (i == 1 || i == 2)
        {
            cd_range(parameters, CD_PAD_LBA + 20, CD_PAD_LBA + 22);
            cd_arm(CD_READ, 3);
            cd_send(&log, CD_READ | CD_STATUS, parameters, 0);
            log.lba = CD_PAD_LBA + 20;
            cd_collect(&log, 8000000u, 3000000u, 8000000u, i == 1 ? CD_DMA | CD_HOLD_DMA : CD_HOLD_DMA);

            if (i == 1)
                cd_drain(&log, 0);
            else
                out8(CD_TRANSFER, 0x10);
        }
        else if (i == 3)
        {
            cd_masks(0);
            cd_play(&log, cd_track(7), cd_track_end(7) + 1, 0);
            tb_delay(2000000u);
            cd_subq(&subq);
            entry->before = subq.absolute;
            cd_masks(CD_CONTROL_SMIM | CD_CONTROL_DEIM);
        }

        cd_begin(&log, 0xFF);
        cd_reset();
        entry->master[0] = cd_master();
        entry->irq[0] = cd_irq_line();
        tb_delay(1000);
        entry->master[1] = cd_master();
        entry->irq[1] = cd_irq_line();
        out8(CD_MASTER, CD_CONTROL_SMIM | CD_CONTROL_DEIM);
        cd_poll(&log, 500000u);
        cd_keep(&entry->log, &log);
        out8(0x00AF, 0x0F);
        cd_masks(0);

        if (i == 3)
        {
            tb_delay(500000u);
            cd_subq(&subq);
            entry->after = subq.absolute;
        }

        recover(result);
    }
}

// A 64-sector read delivering its fourth sector when the other command is written, without waiting for DRY
static void step_during(errors_t* result)
{
    for (int i = 0; i < DURING; i++)
    {
        uint8_t parameters[8];
        cd_log_t log;

        shell_step("DURING A READ: %s", k_during[i]);
        cd_range(parameters, CD_PAD_LBA + 200, CD_PAD_LBA + 263);
        cd_arm(CD_READ, 64);
        cd_send(&log, CD_READ | CD_STATUS, parameters, 0);
        log.lba = CD_PAD_LBA + 200;
        cd_stop_ready(4);
        cd_collect(&log, 8000000u, 3000000u, 8000000u, CD_DMA | CD_UNTIL_END);
        cd_stop_ready(0);

        result->during_ready[i] = log.ready;

        if (i == 0)
            memset(parameters, 0, sizeof(parameters));
        else if (i == 1)
            cd_range(parameters, 5000, 5000);
        else
            cd_range(parameters, 3000, 3000);

        cd_send(&log, (uint8_t)((i == 0 ? CD_STATE : i == 1 ? CD_SEEK : CD_READ) | CD_STATUS), parameters,
            CD_NO_WAIT);
        log.lba = 3000;
        cd_collect(&log, 3000000u, 1000000u, 6000000u, CD_DMA);
        out8(0x00AF, 0x0F);
        cd_keep(&result->during[i], &log);
        recover(result);
    }
}

// HT-50 q6: 80h+STATUS written 10 us, 500 us and 2 ms after the first packet of a TOC reply, the rest of the TOC
// unread; then eight lone 80h+STATUS for the time to their first packet
static void step_leftover(errors_t* result)
{
    for (int i = 0; i < LEFTOVERS; i++)
    {
        shell_step("LEFTOVER TOC, 80H AFTER %u US", k_leftover_us[i]);
        leftover(&result->leftovers[i], k_leftover_us[i]);
        recover(result);
    }

    result->lone_min = 0xFFFFFFFFu;

    for (int i = 0; i < LONE_RUNS; i++)
    {
        cd_log_t log;

        shell_step("LONE 80H %u/%u", i + 1, LONE_RUNS);
        cd_state(&log);

        if (log.count == 0)
            continue;

        uint32_t first = log.packets[0].time;
        result->lone_min = first < result->lone_min ? first : result->lone_min;
        result->lone_max = first > result->lone_max ? first : result->lone_max;
        result->lone_runs++;
    }
}

// Old TOC packets are 16h and 17h; the reply is the first packet that isn't
static void leftover(leftover_t* entry, uint32_t delay)
{
    static const uint8_t k_zero[8];
    cd_log_t log;

    entry->reply = NO_REPLY;
    cd_send(&log, CD_TOC | CD_STATUS, k_zero, 0);

    uint32_t start = tb_now();

    while ((cd_master() & CD_MASTER_SRQ) == 0)
    {
        if (tb_now() - start > 2000000u)
            return;
    }

    for (int k = 0; k < 4; k++)
        entry->first[k] = in8(CD_COMMAND);

    uint32_t read = tb_now();
    tb_delay(delay);
    entry->master = cd_master();
    cd_send(&log, CD_STATE | CD_STATUS, k_zero, CD_NO_WAIT | CD_KEEP_QUEUE);
    entry->write = log.start - read;
    cd_collect(&log, 1000000u, 100000u, 3000000u, 0);
    cd_keep(&entry->log, &log);
    entry->total = log.total;

    for (int k = 0; k < log.count; k++)
    {
        uint8_t kind = log.packets[k].bytes[0];

        if (kind == 0x16 || kind == 0x17)
        {
            if (entry->reply == NO_REPLY)
                entry->old++;

            continue;
        }

        if (entry->reply == NO_REPLY)
        {
            entry->reply = (uint8_t)k;
            entry->reply_time = log.packets[k].time;
        }
    }
}

static const char* leftover_verdict(const leftover_t* entry)
{
    if (entry->reply == NO_REPLY)
        return entry->total != 0 ? "OLD TOC, NO REPLY" : "NOTHING";

    return entry->old == 0 ? "OLD TOC GONE" : "OLD TOC FIRST";
}

static void step_idle(errors_t* result)
{
    shell_step("IDLE PORT READS");

    for (int i = 0; i < IDLE_PORTS; i++)
        result->idle_ports[i] = in8(k_ports[i]);
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 13;
}

static void page(const void* data, uint32_t steps, int number)
{
    const errors_t* result = (const errors_t*)data;
    int row = SHELL_BODY_TOP;
    char label[32];

    if (number <= 1)
    {
        int first = number == 0 ? 0 : 3;
        int last = number == 0 ? 3 : OUTSIDE;

        for (int i = first; i < last; i++)
        {
            fmt_format(label, sizeof(label), "%s, OK %u/%u,", k_outside[i], result->good_before[i],
                result->good_after[i]);
            row = cd_draw_keep(row, label, &result->outside[i]);
        }

        if (number == 1)
        {
            shell_line(row++, COLOR_GRAY, "OK N/M: ATTEMPT OF THE GOOD READS");
        }

        return;
    }

    if (number <= 3)
    {
        int first = number == 2 ? 0 : 3;
        int last = number == 2 ? 3 : MALFORMED;


        for (int i = first; i < last; i++)
        {
            fmt_format(label, sizeof(label), "%s, LBA %u,", k_malformed[i], result->malformed_lba[i] == CD_NO_LBA ? 0 :
                result->malformed_lba[i]);
            row = cd_draw_keep(row, label, &result->malformed[i]);
        }

        if (number == 3)
            shell_line(row++, COLOR_GRAY, "LBA 1000 AFTER 10: THE WINDOW SLIDES");

        return;
    }

    if (number <= 5)
    {
        int first = number == 4 ? 0 : 5;
        int last = number == 4 ? 5 : PLAIN;

        for (int i = first; i < last; i++)
            row = cd_draw_keep(row, i < PLAIN - 1 ? "NO STATUS" : "SIRQ PENDING", &result->plain[i]);

        if (number == 5)
            shell_line(row++, COLOR_GRAY, "0000 0400: INHERITS STATUS AND IRQ");

        return;
    }

    if (number <= 7)
    {
        int first = number == 6 ? 0 : 26;

        shell_line(row++, COLOR_CYAN, "UNKNOWN COMMANDS, PACKETS, MS");

        for (int i = first; i < first + 26 && i < UNKNOWN; i += 2)
        {
            char text[2][20];

            for (int k = 0; k < 2; k++)
            {
                const unknown_t* entry = &result->unknown[i + k];

                if (entry->count == 0)
                    fmt_format(text[k], sizeof(text[k]), "%02X -", entry->command);
                else
                    fmt_format(text[k], sizeof(text[k]), "%02X %02X%02X%c%u %u.%u", entry->command, entry->bytes[0][0],
                        entry->bytes[0][1], entry->count > 1 ? '+' : ' ', entry->count, entry->time / 10,
                        entry->time % 10);
            }

            shell_line(row++, COLOR_WHITE, "%-18s %s", text[0], text[1]);
        }

        if (number == 7)

        return;
    }

    if (number == 8)
    {
        row = cd_draw_keep(row, "1F P0=3", &result->special[0]);
        row = cd_draw_keep(row, "9F 5F FC 5F FC", &result->special[1]);
        shell_line(row++, COLOR_GRAY, "ON AN MX 1F P0=3: 0000 1841 1900...");
        shell_line(row++, COLOR_GRAY, "WINDOWS 95 EXPECTS 0000 1F5F");
        row++;
        shell_line(row++, COLOR_CYAN, "IDLE READS 04C0-04C6 04C8-04CD");
        shell_line(row++, COLOR_WHITE, "%02X %02X %02X %02X  %02X %02X %02X %02X", result->idle_ports[0],
            result->idle_ports[1], result->idle_ports[2], result->idle_ports[3], result->idle_ports[4],
            result->idle_ports[5], result->idle_ports[6], result->idle_ports[7]);
        shell_line(row++, COLOR_WHITE, "SRST NEEDED TO RECOVER: %u", result->resets);
        return;
    }

    if (number <= 10)
    {
        int first = number == 9 ? 0 : 2;

        for (int i = first; i < first + 2; i++)
        {
            const srst_t* entry = &result->srst[i];
            shell_line(row++, COLOR_CYAN, "SRST %s", k_srst[i]);

            if (i == 2 && (steps & 0x100) == 0)
            {
                shell_line(row++, COLOR_GRAY, "STEP 9 NOT RUN");
                continue;
            }

            shell_line(row++, COLOR_WHITE, "04C0H %02X IRQ %u, 1 MS %02X IRQ %u", entry->master[0], entry->irq[0],
                entry->master[1], entry->irq[1]);
            row = cd_draw_packets(row, entry->log.packets, entry->log.count);

            if (i == 3)
                shell_line(row++, COLOR_WHITE, "SUB-Q %u>%u %s", entry->before, entry->after,
                    entry->after > entry->before + 37 ? "PLAYS ON" : "STOPPED");
        }

        if (number == 10)
            shell_line(row++, COLOR_GRAY, "DATA BOOK: THE RESET RAISES AN IRQ");

        return;
    }

    if (number == 12)
    {
        shell_line(row++, COLOR_CYAN, "05H, ONE PACKET READ, THEN 80H");

        for (int i = 0; i < LEFTOVERS; i++)
        {
            const leftover_t* entry = &result->leftovers[i];
            uint32_t reply = entry->reply_time;

            shell_line(row++, COLOR_GRAY, "80H %u US AFTER, 04C0H %02X: %u PKTS", entry->write, entry->master,
                entry->total);

            if (entry->reply == NO_REPLY)
                shell_line(row++, COLOR_WHITE, " %s, %u OLD", leftover_verdict(entry), entry->old);
            else
                shell_line(row++, COLOR_WHITE, " %s, %u OLD, REPLY %u.%u MS", leftover_verdict(entry), entry->old,
                    reply / 1000u, reply / 100u % 10u);

            row = cd_draw_packets(row, entry->log.packets, entry->log.count < 4 ? entry->log.count : 4);
        }

        row++;

        if (result->lone_runs != 0)
            shell_line(row++, COLOR_WHITE, "LONE 80H FIRST PACKET %u-%u US", result->lone_min, result->lone_max);
        else
            shell_line(row++, COLOR_GRAY, "LONE 80H: NO PACKET");

        shell_line(row++, COLOR_GRAY, "OLD: 16H/17H READ BEFORE THE REPLY");
        return;
    }

    for (int i = 0; i < DURING; i++)
    {
        fmt_format(label, sizeof(label), "%s AFTER %u SECTORS,", k_during[i], result->during_ready[i]);
        row = cd_draw_keep(row, label, &result->during[i]);
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const errors_t* result = (const errors_t*)data;
    int answered = 0;
    (void)steps;

    for (int i = 0; i < UNKNOWN; i++)
        answered += result->unknown[i].count != 0;

    fmt_format(line, 34, "AUDIO %02X%02X UNKNOWN %u SRST %u", result->outside[0].packets[0].bytes[0],
        result->outside[0].packets[0].bytes[1], answered, result->resets);
    *color = COLOR_WHITE;
}
