#include "cd.h"
#include "cdc.h"
#include "fmt.h"
#include "layout.h"
#include "pic.h"
#include "screen.h"
#include "shell.h"
#include "runtime.h"
#include "timebase.h"
#include "towns.h"

#define READY_TIMEOUT 5000000u
#define DMA_TIMEOUT 100000u
#define DTS_RISE 100u
#define READ_FIRST 8000000u
#define READ_QUIET 3000000u
#define COMMAND_FIRST 2000000u
#define COMMAND_QUIET 100000u
#define STALE_LIMIT 64

extern uint8_t __data_start[];

cd_toc_t g_cd_toc;

static uint8_t s_masks;
static uint32_t s_offset;
static uint32_t* s_sink;
static int s_sink_capacity;
static int s_ready_stop;
static uint8_t s_used SHELL_STATE;
static cd_keep_t s_first SHELL_STATE;
static uint32_t s_first_time SHELL_STATE;

// The layout mkimage.py and tracks/mktracks.py build, for when the TOC can't be read; track 4's INDEX 00 is 150
// sectors before its start
static const uint32_t k_starts[CD_TRACKS + 1] = { 0, 0, 40150, 41650, 43900, 44650, 45775, 46900 };
static const uint8_t k_controls[CD_TRACKS + 1] = { 0, 4, 0, 0, 0, 1, 0, 0 };
static const uint32_t k_leadout = 51400;

static void add_packet(cd_log_t* log, uint32_t time, const uint8_t* bytes);
static void run_dma(cd_log_t* log, uint32_t index);
static void first_read(void);

// The collection ends once this many sectors were ready (0: no limit)
void cd_stop_ready(int count)
{
    s_ready_stop = count;
}

const cd_keep_t* cd_first_read(uint32_t* when)
{
    *when = s_first_time;
    return &s_first;
}

static void first_read(void)
{
    cd_log_t log;

    s_first_time = tb_now();
    cd_read(&log, CD_READ | CD_STATUS, 1, 1);
    cd_keep(&s_first, &log);
}

// Every data ready's time goes here as well, for reads longer than a log
void cd_sink(uint32_t* times, int capacity)
{
    s_sink = times;
    s_sink_capacity = capacity;
}

void cd_masks(uint8_t masks)
{
    s_masks = masks & (CD_CONTROL_SMIM | CD_CONTROL_DEIM);
    out8(CD_MASTER, s_masks);
}

uint8_t cd_master(void)
{
    return in8(CD_MASTER);
}

int cd_used(void)
{
    return s_used;
}

uint8_t cd_bcd(uint32_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

uint32_t cd_from_bcd(uint8_t value)
{
    return (uint32_t)(value >> 4) * 10 + (value & 0x0F);
}

void cd_msf(uint32_t lba, uint8_t* msf)
{
    uint32_t frames = lba + 150;
    msf[0] = cd_bcd(frames / 4500);
    msf[1] = cd_bcd((frames / 75) % 60);
    msf[2] = cd_bcd(frames % 75);
}

uint32_t cd_lba(const uint8_t* msf)
{
    uint32_t frames = cd_from_bcd(msf[0]) * 4500 + cd_from_bcd(msf[1]) * 75 + cd_from_bcd(msf[2]);
    return frames >= 150 ? frames - 150 : 0;
}

// Both ends inclusive, as the system ROM reads
void cd_range(uint8_t* parameters, uint32_t first, uint32_t last)
{
    cd_msf(first, parameters);
    cd_msf(last, parameters + 3);
    parameters[6] = 0;
    parameters[7] = 0;
}

// A log that starts now with nothing sent, for packets that follow something other than a command
void cd_begin(cd_log_t* log, uint8_t command)
{
    memset(log, 0, sizeof(*log));
    log->command = command;
    log->lba = CD_NO_LBA;
    log->start = tb_now();
}

void cd_send(cd_log_t* log, uint8_t command, const uint8_t* parameters, uint32_t options)
{
    cd_send_raw(log, command, parameters, 8, options);
}

// Packets left from before are kept as stale, the first one in the log
// The first command since boot is a one-sector read of LBA 1, as 1.6 did, whichever test sends it
void cd_send_raw(cd_log_t* log, uint8_t command, const uint8_t* parameters, int count, uint32_t options)
{
    if (!s_used)
    {
        s_used = 1;
        first_read();
    }

    memset(log, 0, sizeof(*log));
    log->command = command;
    log->lba = CD_NO_LBA;

    for (int i = 0; i < STALE_LIMIT && (cd_master() & CD_MASTER_SRQ) != 0; i++)
    {
        for (int k = 0; k < 4; k++)
        {
            uint8_t value = in8(CD_COMMAND);

            if ((log->flags & CD_LOG_STALE) == 0)
                log->stale[k] = value;
        }

        log->flags |= CD_LOG_STALE;
    }

    if ((options & CD_NO_WAIT) == 0)
    {
        uint32_t start = tb_now();

        while ((cd_master() & CD_MASTER_DRY) == 0)
        {
            if (tb_now() - start > READY_TIMEOUT)
            {
                log->flags |= CD_LOG_NOT_READY;
                break;
            }
        }

        log->ready_wait = tb_now() - start;
    }

    out8(CD_MASTER, (uint8_t)(CD_CONTROL_SIRQ | CD_CONTROL_DEI | s_masks));

    for (int i = 0; i < count; i++)
        out8(CD_PARAMETER, parameters[i]);

    out8(CD_COMMAND, command);
    log->start = tb_now();
    s_used = 1;
}

// Until first passes with no packet at all, quiet passes after the last one, or limit in all
void cd_collect(cd_log_t* log, uint32_t first, uint32_t quiet, uint32_t limit, uint32_t options)
{
    uint32_t last = log->total != 0 ? tb_now() : log->start;
    uint32_t sectors = 0;

    for (;;)
    {
        uint32_t now = tb_now();
        uint32_t elapsed = now - log->start;

        if ((cd_master() & CD_MASTER_SRQ) != 0)
        {
            uint8_t bytes[4];

            for (int i = 0; i < 4; i++)
                bytes[i] = in8(CD_COMMAND);

            add_packet(log, elapsed, bytes);
            last = now;

            if (bytes[0] == 0x21)
                log->flags |= CD_LOG_ERROR;

            if (bytes[0] == 0x22)
            {
                if (s_sink != 0 && log->ready < s_sink_capacity)
                    s_sink[log->ready] = elapsed;

                log->ready++;
            }

            if (bytes[0] == 0x22 && (options & CD_DMA) != 0)
                run_dma(log, sectors++);

            if (s_ready_stop != 0 && log->ready >= s_ready_stop)
                break;

            if ((options & CD_UNTIL_END) != 0 && (bytes[0] == 0x06 || bytes[0] == 0x21))
            {
                log->flags |= CD_LOG_DONE;
                break;
            }

            if ((options & CD_UNTIL_SEEK) != 0 && (bytes[0] == 0x04 || bytes[0] == 0x21))
            {
                log->flags |= CD_LOG_DONE;
                break;
            }

            if ((options & CD_HOLD_DMA) != 0 && bytes[0] == 0x22)
                break;

            continue;
        }

        if (elapsed >= limit)
        {
            log->flags |= CD_LOG_LIMIT;
            break;
        }

        if (log->total == 0 ? elapsed >= first : now - last >= quiet)
            break;
    }

    log->end = tb_now() - log->start;
}

// Every packet that comes in the next microseconds, added to the log
void cd_poll(cd_log_t* log, uint32_t microseconds)
{
    uint32_t start = tb_now();

    while (tb_now() - start < microseconds)
    {
        if ((cd_master() & CD_MASTER_SRQ) == 0)
            continue;

        uint8_t bytes[4];
        uint32_t now = tb_now();

        for (int i = 0; i < 4; i++)
            bytes[i] = in8(CD_COMMAND);

        add_packet(log, now - log->start, bytes);
    }
}

// The DMA of a sector that is ready, started now; the time it took, or CD_NO_LBA if it never ended
uint32_t cd_drain(cd_log_t* log, uint32_t index)
{
    uint32_t start = tb_now();
    run_dma(log, index);
    return (log->flags & CD_LOG_DMA_TIMEOUT) != 0 ? CD_NO_LBA : tb_now() - start;
}

// Byte transfers on channel 3 as the system ROM programs them (mode 54h, auto-init), one sector of the command's size
void cd_arm(uint8_t command, uint32_t count)
{
    uint32_t size = 2048;
    (void)count;

    s_offset = 0;

    if ((command & 0x1F) == CD_RAW_READ)
    {
        size = 2340;
        s_offset = 4;
    }
    else if ((command & 0x1F) == CD_MODE2_READ)
        size = 2336;

    cdc_dma_setup(3, CD_BUFFER, (uint16_t)(size - 1), 0x54);
}

void cd_command(cd_log_t* log, uint8_t command, const uint8_t* parameters)
{
    cd_send(log, command, parameters, 0);
    cd_collect(log, COMMAND_FIRST, COMMAND_QUIET, COMMAND_FIRST + 1000000u, 0);
}

void cd_read(cd_log_t* log, uint8_t command, uint32_t lba, uint32_t count)
{
    uint8_t parameters[8];

    cd_range(parameters, lba, lba + count - 1);
    cd_arm(command, count);
    cd_send(log, command, parameters, 0);
    log->lba = lba;
    cd_collect(log, READ_FIRST, READ_QUIET, READ_FIRST + count * 200000u, CD_DMA | CD_UNTIL_END);
    out8(0x00AF, 0x0F);
}

int cd_read_retry(cd_log_t* log, uint8_t command, uint32_t lba, uint32_t count, int attempts)
{
    for (int i = 1; i <= attempts; i++)
    {
        cd_read(log, command, lba, count);

        if (cd_read_ok(log, count))
            return i;
    }

    return 0;
}

int cd_read_ok(const cd_log_t* log, uint32_t count)
{
    if ((log->flags & (CD_LOG_ERROR | CD_LOG_DMA_TIMEOUT | CD_LOG_BAD_DATA | CD_LOG_LIMIT)) != 0 ||
        (log->flags & CD_LOG_DONE) == 0)
        return 0;

    return log->sectors == count;
}

// The end is exclusive; P6 = 1 repeats the range
void cd_play(cd_log_t* log, uint32_t first, uint32_t end, int repeat)
{
    uint8_t parameters[8];

    cd_msf(first, parameters);
    cd_msf(end, parameters + 3);
    parameters[6] = repeat ? 0x01 : 0x00;
    parameters[7] = 0;
    cd_send(log, CD_PLAY | CD_STATUS, parameters, 0);
    cd_collect(log, 3000000u, COMMAND_QUIET, 4000000u, 0);
}

void cd_state(cd_log_t* log)
{
    static const uint8_t k_zero[8];
    cd_send(log, CD_STATE | CD_STATUS, k_zero, 0);
    cd_collect(log, 1000000u, 50000u, 2000000u, 0);
}

void cd_stop(cd_log_t* log)
{
    static const uint8_t k_zero[8];
    cd_send(log, CD_STOP | CD_STATUS, k_zero, 0);
    cd_collect(log, 2000000u, COMMAND_QUIET, 3000000u, 0);
}

// 00 xx, then 18 00 track index, 19 relative M S F, 19 00 absolute M S, 20 absolute F
int cd_subq(cd_subq_t* subq)
{
    static const uint8_t k_zero[8];
    cd_log_t log;
    int found = 0;

    memset(subq, 0, sizeof(*subq));
    cd_send(&log, CD_SUBQ | CD_STATUS, k_zero, 0);
    cd_collect(&log, 500000u, 30000u, 600000u, 0);

    for (int i = 0; i < log.count; i++)
    {
        const uint8_t* bytes = log.packets[i].bytes;

        if (i == 0)
            subq->status = bytes[1];

        if (bytes[0] == 0x18 && found == 0)
        {
            subq->track = (uint8_t)cd_from_bcd(bytes[2]);
            subq->index = bytes[3];
            memcpy(subq->raw, bytes, 4);
            found = 1;
        }
        else if (bytes[0] == 0x19 && found == 1)
        {
            subq->relative = cd_from_bcd(bytes[1]) * 4500 + cd_from_bcd(bytes[2]) * 75 + cd_from_bcd(bytes[3]);
            memcpy(subq->raw + 4, bytes, 4);
            found = 2;
        }
        else if (bytes[0] == 0x19 && found == 2)
        {
            subq->absolute = cd_from_bcd(bytes[2]) * 4500 + cd_from_bcd(bytes[3]) * 75;
            memcpy(subq->raw + 8, bytes, 4);
            found = 3;
        }
        else if (bytes[0] == 0x20 && found == 3)
        {
            subq->absolute += cd_from_bcd(bytes[1]);
            memcpy(subq->raw + 12, bytes, 4);
            found = 4;
        }
    }

    subq->valid = found == 4;
    return subq->valid;
}

// 16 control/ADR point and 17 M S F pairs: A0 the first track, A1 the last, A2 the lead-out
int cd_load_toc(cd_log_t* log)
{
    static const uint8_t k_zero[8];
    cd_toc_t* toc = &g_cd_toc;
    uint8_t point = 0;
    uint8_t control = 0;
    uint32_t found = 0;

    memset(toc, 0, sizeof(*toc));
    cd_send(log, CD_TOC | CD_STATUS, k_zero, 0);
    cd_collect(log, COMMAND_FIRST, COMMAND_QUIET, COMMAND_FIRST + 1000000u, 0);
    toc->packets = log->total;

    for (int i = 0; i < log->count; i++)
    {
        const uint8_t* bytes = log->packets[i].bytes;

        if (bytes[0] == 0x16)
        {
            point = bytes[2];
            control = bytes[1] >> 4;
        }
        else if (bytes[0] == 0x17)
        {
            if (point == 0xA0)
                toc->first = (uint8_t)cd_from_bcd(bytes[1]);
            else if (point == 0xA1)
                toc->last = (uint8_t)cd_from_bcd(bytes[1]);
            else if (point == 0xA2)
                toc->leadout = cd_lba(bytes + 1);
            else if (cd_from_bcd(point) >= 1 && cd_from_bcd(point) <= CD_TRACKS)
            {
                uint32_t track = cd_from_bcd(point);
                toc->start[track] = cd_lba(bytes + 1);
                toc->control[track] = control;
                found |= 1u << track;
            }
        }
    }

    toc->valid = found == 0xFE && toc->leadout != 0;

    if (!toc->valid)
    {
        memcpy(toc->start, k_starts, sizeof(k_starts));
        memcpy(toc->control, k_controls, sizeof(k_controls));
        toc->leadout = k_leadout;
    }

    return toc->valid;
}

uint32_t cd_track(int track)
{
    if (g_cd_toc.start[CD_TRACKS] == 0)
        return k_starts[track];

    return g_cd_toc.start[track];
}

// The last sector of a track: track 1 ends with the data, track 3 before track 4's INDEX 00
uint32_t cd_track_end(int track)
{
    if (track == 1)
        return CD_DATA_SECTORS - 1;

    if (track == 3)
        return cd_track(4) - 151;

    if (track == CD_TRACKS)
        return (g_cd_toc.leadout != 0 ? g_cd_toc.leadout : k_leadout) - 1;

    return cd_track(track + 1) - 1;
}

void cd_reset(void)
{
    out8(CD_MASTER, (uint8_t)(CD_CONTROL_SRST | s_masks));
}

// CD-DA stopped, the queue drained, DMA masked and both requests acknowledged, as the next test expects the CDC
void cd_quiet(void)
{
    static const uint8_t k_zero[8];
    cd_log_t log;

    cd_masks(0);
    out8(0x00AF, 0x0F);
    cd_send(&log, CD_STOP | CD_STATUS, k_zero, 0);
    cd_collect(&log, 2000000u, COMMAND_QUIET, 3000000u, 0);

    uint32_t start = tb_now();

    while ((cd_master() & CD_MASTER_DRY) == 0 && tb_now() - start < READY_TIMEOUT)
        ;

    for (int i = 0; i < STALE_LIMIT && (cd_master() & CD_MASTER_SRQ) != 0; i++)
    {
        for (int k = 0; k < 4; k++)
            (void)in8(CD_COMMAND);
    }

    out8(CD_MASTER, CD_CONTROL_SIRQ | CD_CONTROL_DEI);
}

// The LBA a sector's data carries: the tag of the sectors after the payload, or the payload itself in RAM
uint32_t cd_check_sector(uint32_t address, uint32_t expected, int full)
{
    const uint32_t* words = (const uint32_t*)address;

    if (words[0] == CD_TAG_MAGIC)
    {
        uint32_t lba = words[1];
        int checked = full ? 512 : 0;

        if (words[2] != (((lba << 9) | 2) ^ CD_TAG_MIX) || words[511] != (((lba << 9) | 511) ^ CD_TAG_MIX))
            return CD_NO_LBA;

        for (int i = 2; i < checked; i++)
        {
            if (words[i] != ((((uint32_t)lba << 9) | (uint32_t)i) ^ CD_TAG_MIX))
                return CD_NO_LBA;
        }

        return lba & CD_NO_LBA;
    }

    uint32_t text_sectors = ((uint32_t)__data_start - PAYLOAD_BASE) / 2048;

    if (expected >= 1 && expected <= text_sectors &&
        memcmp((const void*)address, (const void*)(PAYLOAD_BASE + (expected - 1) * 2048), 2048) == 0)
        return expected;

    return CD_NO_LBA;
}

const cd_packet_t* cd_find(const cd_log_t* log, uint8_t first, int nth)
{
    for (int i = 0; i < log->count; i++)
    {
        if (log->packets[i].bytes[0] == first && nth-- == 0)
            return &log->packets[i];
    }

    return 0;
}

uint32_t cd_time_of(const cd_log_t* log, uint8_t first, int nth)
{
    const cd_packet_t* packet = cd_find(log, first, nth);
    return packet != 0 ? packet->time : 0xFFFFFFFFu;
}

void cd_brief(cd_brief_t* brief, const cd_log_t* log)
{
    int kept = 0;

    memset(brief, 0, sizeof(*brief));
    brief->command = log->command;
    brief->flags = log->flags;

    for (int i = 0; i < log->count; i++)
    {
        const cd_packet_t* packet = &log->packets[i];

        if (packet->bytes[0] >= CD_PACKET_DMA_NONE)
            continue;

        brief->count++;

        if (kept < 4)
        {
            memcpy(brief->bytes[kept], packet->bytes, 4);
            brief->times[kept] = packet->time;
            kept++;
        }
    }

    if (log->total > log->count)
        brief->count = (uint8_t)(brief->count + log->total - log->count);
}

int cd_brief_same(const cd_brief_t* a, const cd_brief_t* b)
{
    return a->command == b->command && a->count == b->count &&
        memcmp(a->bytes, b->bytes, sizeof(a->bytes)) == 0;
}

uint8_t cd_irq_line(void)
{
    return (uint8_t)((pic_read_irr() >> 9) & 1);
}

void cd_keep(cd_keep_t* keep, const cd_log_t* log)
{
    memset(keep, 0, sizeof(*keep));
    keep->command = log->command;
    keep->count = log->count < CD_KEEP ? log->count : CD_KEEP;
    keep->flags = log->flags;
    keep->stale = log->stale[0];
    memcpy(keep->packets, log->packets, keep->count * sizeof(cd_packet_t));
}

int cd_draw_keep(int row, const char* label, const cd_keep_t* keep)
{
    if (row > SHELL_BODY_BOTTOM - 1)
        return row;

    shell_line(row++, COLOR_CYAN, "%s %02X %s", label, keep->command, cd_flags_name(keep->flags));
    return cd_draw_packets(row, keep->packets, keep->count);
}

// Two packets a line, each with its time in ms from the command write
int cd_draw_packets(int row, const cd_packet_t* packets, int count)
{
    char line[2][20];

    if (count == 0)
    {
        shell_line(row++, COLOR_GRAY, " NO PACKET");
        return row;
    }

    for (int i = 0; i < count; i += 2)
    {
        if (row >= SHELL_BODY_BOTTOM && i + 2 < count)
        {
            shell_line(row++, COLOR_GRAY, " %u MORE IN THE QR CODES", count - i);
            break;
        }

        for (int k = 0; k < 2; k++)
        {
            const cd_packet_t* packet = &packets[i + k];
            line[k][0] = 0;

            if (i + k < count)
                fmt_format(line[k], sizeof(line[k]), "%02X%02X%02X%02X %5u.%u", packet->bytes[0], packet->bytes[1],
                    packet->bytes[2], packet->bytes[3], packet->time / 1000u, packet->time / 100u % 10u);
        }

        shell_line(row++, COLOR_WHITE, "%-17s %s", line[0], line[1]);
    }

    return row;
}

void cd_series_add(cd_series_t* series, const cd_log_t* log, int failed)
{
    cd_brief_t brief;
    uint32_t first = 0xFFFFFFFFu;
    uint32_t last = 0;

    cd_brief(&brief, log);

    if (series->runs == 0)
    {
        series->first = brief;
        series->first_min = 0xFFFFFFFFu;
        series->last_min = 0xFFFFFFFFu;
    }

    series->runs++;
    series->same += cd_brief_same(&series->first, &brief);
    series->failed += failed != 0;

    for (int i = 0; i < log->count; i++)
    {
        if (log->packets[i].bytes[0] >= CD_PACKET_DMA_NONE)
            continue;

        first = first == 0xFFFFFFFFu ? log->packets[i].time : first;
        last = log->packets[i].time;
    }

    if (first == 0xFFFFFFFFu)
        return;

    series->first_min = first < series->first_min ? first : series->first_min;
    series->first_max = first > series->first_max ? first : series->first_max;
    series->last_min = last < series->last_min ? last : series->last_min;
    series->last_max = last > series->last_max ? last : series->last_max;
}

// The first packets' first two bytes in one line
int cd_draw_brief(int row, const char* label, const cd_brief_t* brief)
{
    char text[4][6];

    for (int i = 0; i < 4; i++)
    {
        text[i][0] = 0;

        if (i < brief->count)
            fmt_format(text[i], sizeof(text[i]), "%02X%02X", brief->bytes[i][0], brief->bytes[i][1]);
    }

    shell_line(row++, COLOR_WHITE, "%-6s %-4s %-4s %-4s %-4s%s %u", label, text[0], text[1], text[2], text[3],
        brief->count > 4 ? "+" : " ", brief->count);
    return row;
}

const char* cd_flags_name(uint8_t flags)
{
    if ((flags & CD_LOG_NOT_READY) != 0)
        return "NOT READY";

    if ((flags & CD_LOG_DMA_TIMEOUT) != 0)
        return "DMA TIMEOUT";

    if ((flags & CD_LOG_BAD_DATA) != 0)
        return "BAD DATA";

    if ((flags & CD_LOG_LIMIT) != 0)
        return "TIME LIMIT";

    if ((flags & CD_LOG_ERROR) != 0)
        return "ERROR";

    if ((flags & CD_LOG_STALE) != 0)
        return "STALE";

    return "";
}

static void add_packet(cd_log_t* log, uint32_t time, const uint8_t* bytes)
{
    if (log->total < 255)
        log->total++;

    if (log->count >= CD_LOG_PACKETS)
    {
        log->flags |= CD_LOG_FULL;
        return;
    }

    cd_packet_t* packet = &log->packets[log->count++];
    packet->time = time;
    memcpy(packet->bytes, bytes, 4);
}

static void run_dma(cd_log_t* log, uint32_t index)
{
    uint8_t bytes[4] = { CD_PACKET_DMA, 0, 0, 0 };
    uint32_t start = tb_now();

    MMIO32(CD_BUFFER + s_offset) = 0;
    out8(CD_TRANSFER, 0x10);

    while ((cd_master() & CD_MASTER_DTS) == 0 && tb_now() - start < DTS_RISE)
        ;

    if ((cd_master() & CD_MASTER_DTS) == 0 && tb_now() - start >= DTS_RISE)
    {
        bytes[0] = CD_PACKET_DMA_NONE;
        log->flags |= CD_LOG_DMA_TIMEOUT;
    }

    while ((cd_master() & CD_MASTER_DTS) != 0)
    {
        if (tb_now() - start > DMA_TIMEOUT)
        {
            bytes[0] = CD_PACKET_DMA_TIMEOUT;
            log->flags |= CD_LOG_DMA_TIMEOUT;
            break;
        }
    }

    uint32_t end = tb_now() - log->start;
    uint32_t expected = log->lba != CD_NO_LBA ? log->lba + index : CD_NO_LBA;
    uint32_t lba = cd_check_sector(CD_BUFFER + s_offset, expected, 0);

    if (lba != expected)
        log->flags |= CD_LOG_BAD_DATA;
    else if (bytes[0] == CD_PACKET_DMA)
        log->sectors++;

    bytes[1] = (uint8_t)lba;
    bytes[2] = (uint8_t)(lba >> 8);
    bytes[3] = (uint8_t)(lba >> 16);
    uint8_t total = log->total;
    add_packet(log, end, bytes);
    log->total = total;
}
