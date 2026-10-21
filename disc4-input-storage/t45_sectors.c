#include "tests.h"

// 4.5 Floppy sector reads (docs/12, HT-32), the FDC driven directly with DMA channel 0, read only. The disk's mode is
// found by trying each one; the target is Columns' check (C0 H1 R F7, 512 bytes) when the track has it, otherwise
// the first ID after the index on the chosen track. The ID list carries each ID's time from the index rise, which
// gives the sector positions; Read Track gives the raw bytes of the track

#define IDS 48
#define READS 256
#define KEEP 16
#define TRACKS 3
#define TRACK_MAX 12544
#define TRACK_DMA 16384
#define CHOOSE_US 10000000u
#define COLUMNS_R 0xF7
#define MISSING_R 0xFE
#define FIRST_PAGE_IDS 16
#define TRACK_SCRATCH (FDC_BUFFER + 0x10000u)

typedef struct
{
    uint8_t id[6];
    uint8_t status;
    uint8_t reserved;
    uint32_t time;
} id_entry_t;

typedef struct
{
    uint8_t ran;
    uint8_t cylinder;
    uint8_t side;
    uint8_t side_bit;
    fdc_media_t media;
    uint8_t side_ids[2][4];
    uint8_t side_status[2];
    uint8_t id_count;
    uint8_t target_found;
    uint8_t target[4];
    uint32_t period;
    id_entry_t ids[IDS];
    uint16_t read_count;
    uint16_t reserved;
    uint8_t read_status[READS];
    uint16_t read_time[READS];
    uint32_t read_crc[READS];
    uint8_t read_bytes[READS][KEEP];
    uint16_t track_length[TRACKS];
    uint16_t track_diff[TRACKS];
    uint8_t track_status[TRACKS];
    uint8_t missing_status[2];
    uint8_t not_ready_status;
    uint8_t reserved2[3];
    uint32_t track_crc[TRACKS];
    uint32_t missing_time[2];
    uint32_t not_ready_time;
    uint8_t track[TRACK_MAX];
} sectors_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void choose(sectors_t* result);
static void go_to(sectors_t* result);
static void side_polarity(sectors_t* result);
static void step_ids(sectors_t* result);
static void step_reads(sectors_t* result);
static void step_track(sectors_t* result);
static void step_missing(sectors_t* result);
static uint16_t sector_size(const sectors_t* result);
static int byte_values(const sectors_t* result, int position, uint8_t* values, uint16_t* counts, int capacity);
static int together(const sectors_t* result);
static int distinct_crcs(const sectors_t* result);
static int marks(const uint8_t* track, int length, uint8_t mark);

static const test_step_t k_steps[] =
{
    { "TRACK ID LIST", 0 },
    { "ONE SECTOR READ 256 TIMES", 0 },
    { "READ TRACK X3", 0 },
    { "MISSING SECTOR, NOT READY", 0 },
    { 0, 0 }
};

const test_t g_test_sectors =
{
    "4.5", "FLOPPY SECTOR READS", "DISK", 0, 1, sizeof(sectors_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    sectors_t* result = (sectors_t*)data;

    ui_pad_only(0);
    shell_step("FINDING THE DISK'S MODE");
    ui_prompt("FINDING THE DISK'S MODE", 0);
    fdc_detect(&result->media);

    if (!result->media.found)
    {
        fdc_off();
        return;
    }

    result->ran |= 0x80;
    result->period = result->media.period;
    choose(result);
    side_polarity(result);
    go_to(result);

    if ((steps & 0x01) != 0)
        step_ids(result);

    if ((steps & 0x02) != 0)
        step_reads(result);

    if ((steps & 0x04) != 0)
        step_track(result);

    if ((steps & 0x08) != 0)
        step_missing(result);

    fdc_restore(3, 0);
    fdc_off();
    scr_clear_rows(SHELL_BODY_TOP, SHELL_BODY_BOTTOM);
}

// Left and Right pick the cylinder, Up and Down the side; A or ten seconds go on
static void choose(sectors_t* result)
{
    uint32_t start = tb_now();
    int cylinder = 0;
    int side = 1;

    for (;;)
    {
        int event;

        ui_prompt("TARGET TRACK, COLUMNS: C0 H1", "<> CYLINDER, ^V SIDE, A STARTS");
        ui_note(0, COLOR_WHITE, "MEDIA %s", g_fdc_modes[result->media.mode].name);
        ui_note(1, COLOR_GREEN, "CYLINDER %u SIDE %u", cylinder, side);

        while ((event = input_poll()) == INPUT_NONE && tb_now() - start < CHOOSE_US)
            ui_countdown(start, CHOOSE_US, "");

        if (event == INPUT_NONE || event == INPUT_A)
            break;

        if (event == INPUT_LEFT && cylinder > 0)
            cylinder--;
        else if (event == INPUT_RIGHT && cylinder < 79)
            cylinder++;
        else if (event == INPUT_UP || event == INPUT_DOWN)
            side ^= 1;

        start = tb_now();
    }

    result->cylinder = (uint8_t)cylinder;
    result->side = (uint8_t)side;
}

// Which 0208h bit 2 value reads the IDs that say H = 1 on cylinder 0; without that, bit 2 set is side 1
static void side_polarity(sectors_t* result)
{
    shell_step("SIDE SELECT POLARITY");
    fdc_restore(3, 0);
    result->side_bit = 1;

    for (int bit = 0; bit < 2; bit++)
    {
        uint8_t id[6] = { 0 };

        fdc_side(bit);
        tb_delay(2000);
        result->side_status[bit] = fdc_read_address(0xC0, id, 1200000u, 0);

        for (int i = 0; i < 4; i++)
            result->side_ids[bit][i] = id[i];
    }

    if (result->side_status[0] == 0 && result->side_status[1] == 0 && result->side_ids[0][1] == 1 &&
        result->side_ids[1][1] == 0)
        result->side_bit = 0;
}

static void go_to(sectors_t* result)
{
    fdc_restore(3, 0);

    if (result->cylinder != 0)
        fdc_seek(0, result->cylinder, 0x13, 0);

    out8(FDC_TRACK, result->cylinder);
    fdc_side(result->side ? result->side_bit : !result->side_bit);
    tb_delay(30000u);
}

// Every ID passing the head for two revolutions from an index rise, each with its time from that rise
static void step_ids(sectors_t* result)
{
    uint32_t limit = (result->period != 0 ? result->period : 200000u) * 2 + 20000u;
    uint32_t edge;

    shell_step("TRACK ID LIST");
    fdc_force();
    edge = fdc_index_edge(500000u);

    while (edge != 0 && result->id_count < IDS && tb_now() - edge < limit)
    {
        id_entry_t* entry = &result->ids[result->id_count];
        uint32_t end;

        entry->status = fdc_read_address(0xC0, entry->id, 1200000u, &end);
        entry->time = end - edge;
        result->id_count++;

        if (entry->status == FDC_TIMEOUT)
            break;
    }

    for (int pass = 0; pass < 2 && !result->target_found; pass++)
    {
        for (int i = 0; i < result->id_count && !result->target_found; i++)
        {
            const id_entry_t* entry = &result->ids[i];

            if (entry->status != 0 || (pass == 0 && entry->id[2] != COLUMNS_R))
                continue;

            for (int k = 0; k < 4; k++)
                result->target[k] = entry->id[k];

            result->target_found = 1;
        }
    }

    result->ran |= 0x01;
}

static uint16_t sector_size(const sectors_t* result)
{
    return (uint16_t)(128u << (result->target[3] & 0x03));
}

static void step_reads(sectors_t* result)
{
    uint16_t size;

    if (!result->target_found)
        return;

    size = sector_size(result);
    out8(FDC_TRACK, result->target[0]);
    out8(FDC_SECTOR, result->target[2]);

    for (int i = 0; i < READS; i++)
    {
        uint32_t elapsed = 0;
        uint16_t moved = 0;

        if ((i & 0x0F) == 0)
            shell_step("SECTOR %02X READ %u/%u", result->target[2], i + 1, READS);

        out8(FDC_SECTOR, result->target[2]);
        result->read_status[i] = fdc_read_dma(0x80, size, 1500000u, &elapsed, &moved);
        result->read_time[i] = (uint16_t)(elapsed / 100u < 0xFFFF ? elapsed / 100u : 0xFFFF);
        result->read_crc[i] = crc32((const void*)FDC_BUFFER, moved);
        memcpy(result->read_bytes[i], (const void*)FDC_BUFFER, KEEP);
        result->read_count = (uint16_t)(i + 1);
    }

    result->ran |= 0x02;
}

// The first copy goes into the record; the others are compared with it
static void step_track(sectors_t* result)
{
    for (int i = 0; i < TRACKS; i++)
    {
        uint32_t elapsed;
        uint16_t moved = 0;
        uint16_t kept;

        shell_step("READ TRACK %u/%u", i + 1, TRACKS);
        memset((void*)FDC_BUFFER, 0, TRACK_DMA);
        result->track_status[i] = fdc_read_dma(0xE0, TRACK_DMA, 1500000u, &elapsed, &moved);
        result->track_length[i] = moved;
        result->track_crc[i] = crc32((const void*)FDC_BUFFER, moved);
        kept = moved < TRACK_MAX ? moved : TRACK_MAX;

        if (i == 0)
            memcpy(result->track, (const void*)FDC_BUFFER, kept);

        result->track_diff[i] = 0xFFFF;

        for (uint16_t k = 0; k < kept && i != 0; k++)
        {
            if (MMIO8(FDC_BUFFER + k) != result->track[k])
            {
                result->track_diff[i] = k;
                break;
            }
        }
    }

    result->ran |= 0x04;
}

// A sector the track doesn't have, twice; then a read with the motor off for 1.5 s
static void step_missing(sectors_t* result)
{
    shell_step("MISSING SECTOR");
    out8(FDC_TRACK, result->cylinder);

    for (int i = 0; i < 2; i++)
    {
        uint16_t moved;
        out8(FDC_SECTOR, MISSING_R);
        result->missing_status[i] = fdc_read_dma(0x80, 128, 3000000u, &result->missing_time[i], &moved);
    }

    shell_step("NOT READY");
    fdc_motor(0);
    tb_delay(1500000u);
    out8(FDC_SECTOR, 1);

    uint16_t moved;
    result->not_ready_status = fdc_read_dma(0x80, 128, 3000000u, &result->not_ready_time, &moved);
    fdc_motor(1);
    tb_delay(500000u);
    result->ran |= 0x08;
}

// The distinct values a byte position took over the reads with their counts, most frequent first
static int byte_values(const sectors_t* result, int position, uint8_t* values, uint16_t* counts, int capacity)
{
    int distinct = 0;

    for (int i = 0; i < result->read_count; i++)
    {
        uint8_t value = result->read_bytes[i][position];
        int k;

        for (k = 0; k < distinct && values[k] != value; k++)
            ;

        if (k == distinct)
        {
            if (distinct == capacity)
                continue;

            values[distinct] = value;
            counts[distinct] = 0;
            distinct++;
        }

        counts[k]++;
    }

    for (int i = 0; i < distinct; i++)
    {
        for (int j = i + 1; j < distinct; j++)
        {
            if (counts[j] > counts[i])
            {
                uint16_t count = counts[i];
                uint8_t value = values[i];
                counts[i] = counts[j];
                values[i] = values[j];
                counts[j] = count;
                values[j] = value;
            }
        }
    }

    return distinct;
}

// 1 when every read has the varying bytes all at their most common values or all away from them
static int together(const sectors_t* result)
{
    uint8_t common[KEEP];
    uint16_t varying = 0;

    for (int position = 0; position < KEEP; position++)
    {
        uint8_t values[8];
        uint16_t counts[8];

        if (byte_values(result, position, values, counts, 8) > 1)
            varying |= (uint16_t)(1 << position);

        common[position] = values[0];
    }

    if (varying == 0)
        return -1;

    for (int i = 0; i < result->read_count; i++)
    {
        uint16_t same = 0;

        for (int position = 0; position < KEEP; position++)
        {
            if ((varying & (1 << position)) != 0 && result->read_bytes[i][position] == common[position])
                same |= (uint16_t)(1 << position);
        }

        if (same != 0 && same != varying)
            return 0;
    }

    return 1;
}

static int distinct_crcs(const sectors_t* result)
{
    int distinct = 0;

    for (int i = 0; i < result->read_count; i++)
    {
        int seen = 0;

        for (int k = 0; k < i && !seen; k++)
            seen = result->read_crc[k] == result->read_crc[i];

        distinct += !seen;
    }

    return distinct;
}

// A1 A1 A1 followed by the mark: the address and data marks Read Track saw
static int marks(const uint8_t* track, int length, uint8_t mark)
{
    int count = 0;

    for (int i = 0; i + 3 < length; i++)
    {
        if (track[i] == 0xA1 && track[i + 1] == 0xA1 && track[i + 2] == 0xA1 && track[i + 3] == mark)
            count++;
    }

    return count;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 6;
}

static void page(const void* data, uint32_t steps, int number)
{
    const sectors_t* result = (const sectors_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if ((result->ran & 0x80) == 0)
    {
        shell_line(row++, COLOR_YELLOW, "NO DISK, OR NO MODE READ AN ID");
        return;
    }

    if (number < 2)
    {
        int first = number == 0 ? 0 : FIRST_PAGE_IDS;
        int last = number == 0 ? FIRST_PAGE_IDS : IDS;

        if (number == 0)
        {
            shell_line(row++, COLOR_WHITE, "MEDIA %s, TURN %u US", g_fdc_modes[result->media.mode].name, result->period);
            shell_line(row++, COLOR_WHITE, "SIDE BIT 0: ST %02X ID %02X %02X %02X %02X", result->side_status[0],
                result->side_ids[0][0], result->side_ids[0][1], result->side_ids[0][2], result->side_ids[0][3]);
            shell_line(row++, COLOR_WHITE, "SIDE BIT 1: ST %02X ID %02X %02X %02X %02X", result->side_status[1],
                result->side_ids[1][0], result->side_ids[1][1], result->side_ids[1][2], result->side_ids[1][3]);
            shell_line(row++, COLOR_GREEN, "SIDE 1 IS 0208H BIT 2 = %u", result->side_bit);
        }

        shell_line(row++, COLOR_CYAN, "C%u H%u IDS: C H R N CRC ST US", result->cylinder, result->side);

        if ((result->ran & 0x01) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        for (int i = first; i < result->id_count && i < last && row <= SHELL_BODY_BOTTOM; i++)
        {
            const id_entry_t* entry = &result->ids[i];
            shell_line(row++, entry->status == 0 ? COLOR_WHITE : COLOR_YELLOW, "%02X %02X %02X %02X %02X%02X %02X %6u",
                entry->id[0], entry->id[1], entry->id[2], entry->id[3], entry->id[4], entry->id[5], entry->status,
                entry->time);
        }

        if (number == 0)
            shell_line(row++, COLOR_GRAY, "%u IDS IN TWO TURNS, TARGET R %02X", result->id_count, result->target[2]);

        return;
    }

    if (number == 2)
    {
        int crc_errors = 0;
        int not_found = 0;
        int sure;

        shell_line(row++, COLOR_CYAN, "R %02X READ %u TIMES, FIRST 16 BYTES", result->target[2], result->read_count);

        if ((result->ran & 0x02) == 0)
        {
            shell_line(row++, COLOR_GRAY, result->target_found ? "NOT RUN" : "NO SECTOR TO READ");
            return;
        }

        for (int position = 0; position < KEEP; position++)
        {
            uint8_t values[8];
            uint16_t counts[8];
            int distinct = byte_values(result, position, values, counts, 8);

            if (distinct == 1)
                shell_line(row++, COLOR_WHITE, "B%02u %u %02X:%u", position, distinct, values[0], counts[0]);
            else
                shell_line(row++, COLOR_YELLOW, "B%02u %u %02X:%u %02X:%u%s", position, distinct, values[0], counts[0],
                    values[1], counts[1], distinct > 2 ? " +" : "");
        }

        for (int i = 0; i < result->read_count; i++)
        {
            crc_errors += (result->read_status[i] & FDC_CRC) != 0;
            not_found += (result->read_status[i] & FDC_RNF) != 0;
        }

        sure = together(result);
        shell_line(row++, COLOR_WHITE, "CRC ERRORS %u, NOT FOUND %u", crc_errors, not_found);
        shell_line(row++, COLOR_WHITE, "%u DISTINCT SECTORS (CRC-32)", distinct_crcs(result));
        shell_line(row++, COLOR_GREEN, "BYTES CHANGE %s", sure < 0 ? "NEVER" : sure ? "TOGETHER" : "INDEPENDENTLY");
        return;
    }

    if (number == 3)
    {
        shell_line(row++, COLOR_CYAN, "READS: STATUS, MS, FIRST 8 BYTES");

        if ((result->ran & 0x02) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        for (int i = 0; i < result->read_count && row <= SHELL_BODY_BOTTOM; i++)
        {
            const uint8_t* bytes = result->read_bytes[i];
            shell_line(row++, COLOR_WHITE, "%02X %4u.%u %02X%02X%02X%02X%02X%02X%02X%02X", result->read_status[i],
                result->read_time[i] / 10, result->read_time[i] % 10, bytes[0], bytes[1], bytes[2], bytes[3],
                bytes[4], bytes[5], bytes[6], bytes[7]);
        }

        return;
    }

    if (number == 4)
    {
        int length = result->track_length[0] < TRACK_MAX ? result->track_length[0] : TRACK_MAX;

        shell_line(row++, COLOR_CYAN, "READ TRACK: BYTES, CRC-32, DIFF");

        if ((result->ran & 0x04) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        for (int i = 0; i < TRACKS; i++)
            shell_line(row++, COLOR_WHITE, "%u: %5u ST %02X %08X %s%5u", i + 1, result->track_length[i],
                result->track_status[i], result->track_crc[i], result->track_diff[i] == 0xFFFF ? "SAME " : "AT ",
                result->track_diff[i] == 0xFFFF ? 0 : result->track_diff[i]);

        shell_line(row++, COLOR_WHITE, "MARKS: ID FE %u, DATA FB %u F8 %u", marks(result->track, length, 0xFE),
            marks(result->track, length, 0xFB), marks(result->track, length, 0xF8));
        shell_line(row++, COLOR_CYAN, "FIRST BYTES");

        for (int i = 0; i < 160 && i < length && row <= SHELL_BODY_BOTTOM; i += 12)
        {
            char line[40];
            int written = 0;

            for (int k = i; k < i + 12 && k < length; k++)
                written += fmt_format(line + written, (int)sizeof(line) - written, "%02X ", result->track[k]);

            shell_line(row++, COLOR_WHITE, "%s", line);
        }

        return;
    }

    shell_line(row++, COLOR_CYAN, "MISSING SECTOR AND NOT READY");

    if ((result->ran & 0x08) == 0)
    {
        shell_line(row++, COLOR_GRAY, "NOT RUN");
        return;
    }

    for (int i = 0; i < 2; i++)
        shell_line(row++, COLOR_WHITE, "R %02X: ST %02X AFTER %u MS", MISSING_R, result->missing_status[i],
            result->missing_time[i] / 1000);

    if (result->period != 0)
        shell_line(row++, COLOR_GREEN, "= %u.%u TURNS", result->missing_time[0] / result->period,
            result->missing_time[0] * 10 / result->period % 10);

    shell_line(row++, COLOR_WHITE, "MOTOR OFF 1.5 S: ST %02X AFTER %u US", result->not_ready_status,
        result->not_ready_time);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const sectors_t* result = (const sectors_t*)data;
    (void)steps;

    *color = COLOR_WHITE;

    if ((result->ran & 0x80) == 0)
        fmt_copy(line, "NO DISK", 34);
    else if ((result->ran & 0x02) != 0)
    {
        int sure = together(result);
        fmt_format(line, 34, "R %02X: %u KINDS, %s", result->target[2], distinct_crcs(result),
            sure < 0 ? "STEADY" : sure ? "TOGETHER" : "INDEPENDENT");
    }
    else
        fmt_format(line, 34, "%s, %u IDS", g_fdc_modes[result->media.mode].name, result->id_count);
}
