#include "tests.h"

// 3.9 CDC transfer and interrupts (docs/12, HT-49): 04C0h and the IRQ9 line through a read, the DMA time of a sector
// with the CPU idle and storing to VRAM and RAM, which events raise IRQ9 under each SMIM/DEIM and STATUS/IRQ
// combination and what clears them, whether the next data ready waits for an unacknowledged DMA end, what happens to
// an undrained sector, the packets of a 3-sector read, and how many bytes a sector of each read form moves

#define MARKS 48
#define CASE_MARKS 20
#define CASES 16
#define DEFERRED 4
#define UNDRAINED 6
#define FORMS 3
#define DMA_CASES 3
#define DMA_REPEATS 8
#define HEAD_BYTES 16
#define END_TAIL_US 20000u
#define TIMELINE_LIMIT 8000000u
#define NO_ACK 0xFFFFFFFFu
#define PROBE_COUNT 4095u
#define VRAM_TARGET 0x7C000u
#define RAM_TARGET 0x00180000u
#define TIMELINE_LBA (CD_PAD_LBA + 1000)

enum
{
    MARK_MASTER,
    MARK_PACKET,
    MARK_DMA,
    MARK_DMA_END,
    MARK_ACK
};

typedef struct
{
    uint32_t stamp;
    uint8_t bytes[4];
} mark_t;

typedef struct
{
    uint8_t count;
    uint8_t full;
    uint8_t masks;
    uint8_t command;
    mark_t marks[MARKS];
} timeline_t;

typedef struct
{
    uint8_t count;
    uint8_t full;
    uint8_t masks;
    uint8_t command;
    mark_t marks[CASE_MARKS];
} case_t;

typedef struct
{
    timeline_t timelines[2];
    uint32_t dma_low[DMA_CASES];
    uint32_t dma_high[DMA_CASES];
    uint32_t stores[DMA_CASES];
    uint8_t dma_failed[DMA_CASES];
    uint8_t reserved;
    case_t cases[CASES];
    uint8_t clears[8];
    uint8_t clears_irq[8];
    timeline_t deferred[DEFERRED];
    cd_keep_t undrained[UNDRAINED];
    uint32_t undrained_lba[UNDRAINED];
    uint32_t undrained_dma[UNDRAINED];
    cd_keep_t multi;
    uint16_t sizes[FORMS];
    uint8_t size_flags[FORMS];
    uint8_t reserved2;
    uint8_t heads[FORMS][HEAD_BYTES];
    cd_keep_t short_count;
    uint16_t cpu_bytes;
    uint8_t cpu_master[2];
    uint32_t cpu_time;
    uint32_t cpu_lba;
    cd_keep_t cpu;
} transfer_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static int timeline(mark_t* marks, int capacity, uint8_t* full, uint8_t command, uint32_t lba, uint32_t sectors,
    uint8_t masks, uint32_t dei_delay, int acks);
static void step_dma(transfer_t* result);
static void step_cases(transfer_t* result);
static void step_clears(transfer_t* result);
static void step_deferred(transfer_t* result);
static void step_undrained(transfer_t* result);
static void step_sizes(transfer_t* result);
static void step_short(transfer_t* result);
static void step_cpu(transfer_t* result);
static int wait_ready(cd_log_t* log, uint32_t lba, uint8_t command);
static uint16_t dma_current(void);
static int draw_marks(int row, const mark_t* marks, int count, int first);
static const char* event_irq(const case_t* entry, uint8_t first);

static const test_step_t k_steps[] =
{
    { "04C0H TIMELINE", 0 },
    { "DMA TIME, CPU STORES", 0 },
    { "IRQ MATRIX", 0 },
    { "WHAT CLEARS SIRQ, DEI", 0 },
    { "DMA END NOT ACKED", 0 },
    { "UNDRAINED SECTOR", 0 },
    { "3-SECTOR PACKETS", 0 },
    { "SECTOR SIZE BY DMA COUNT", 0 },
    { "CPU TRANSFER, 04C4H", 0 },
    { 0, 0 }
};

static const uint32_t k_deferred_us[DEFERRED] = { 0, 5000, 20000, 50000 };
static const uint32_t k_undrained_us[UNDRAINED] = { 10000, 30000, 60000, 100000, 200000, 500000 };
static const uint8_t k_forms[FORMS] = { CD_READ, CD_RAW_READ, CD_MODE2_READ };
static const char* const k_dma_names[DMA_CASES] = { "IDLE", "VRAM STORES", "RAM STORES" };

const test_t g_test_transfer =
{
    "3.9", "CDC TRANSFER AND IRQ", 0, TEST_RUN_ALL, 1, sizeof(transfer_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    transfer_t* result = (transfer_t*)data;
    cd_log_t log;

    cd_quiet();

    if ((steps & 0x01) != 0)
    {
        for (int i = 0; i < 2; i++)
        {
            timeline_t* entry = &result->timelines[i];
            shell_step("04C0H TIMELINE, %u SECTORS", i == 0 ? 1 : 3);
            entry->masks = CD_CONTROL_SMIM | CD_CONTROL_DEIM;
            entry->command = CD_READ | CD_STATUS | CD_IRQ;
            entry->count = (uint8_t)timeline(entry->marks, MARKS, &entry->full, entry->command, TIMELINE_LBA,
                i == 0 ? 1 : 3, entry->masks, 0, 1);
            cd_quiet();
        }
    }

    if ((steps & 0x02) != 0)
        step_dma(result);

    if ((steps & 0x04) != 0)
        step_cases(result);

    if ((steps & 0x08) != 0)
        step_clears(result);

    if ((steps & 0x10) != 0)
        step_deferred(result);

    if ((steps & 0x20) != 0)
        step_undrained(result);

    if ((steps & 0x40) != 0)
    {
        shell_step("3-SECTOR READ");
        cd_read(&log, CD_READ | CD_STATUS, CD_PAD_LBA + 50, 3);
        cd_keep(&result->multi, &log);
        cd_quiet();
    }

    if ((steps & 0x80) != 0)
        step_sizes(result);

    if ((steps & 0x100) != 0)
        step_cpu(result);

    cd_quiet();
}

static void add_mark(mark_t* marks, int capacity, int* count, uint8_t* full, uint32_t time, uint8_t kind,
    uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    if (*count >= capacity)
    {
        *full = 1;
        return;
    }

    mark_t* mark = &marks[(*count)++];
    mark->stamp = ((time < 0xFFFFFFu ? time : 0xFFFFFFu) << 8) | kind;
    mark->bytes[0] = a;
    mark->bytes[1] = b;
    mark->bytes[2] = c;
    mark->bytes[3] = d;
}

// One read run as a driver would: SIRQ acknowledged after each packet, the DMA started at each data ready and DEI
// acknowledged dei_delay after its end; acks marks the acknowledgements too. Each packet and DMA end mark holds 04C0h and IRQ9 right after it, before the
// acknowledgement
static int timeline(mark_t* marks, int capacity, uint8_t* full, uint8_t command, uint32_t lba, uint32_t sectors,
    uint8_t masks, uint32_t dei_delay, int acks)
{
    uint8_t parameters[8];
    cd_log_t log;
    int count = 0;
    uint8_t last_master = 0xFF;
    uint8_t last_irq = 0xFF;
    int dma = 0;
    int pending = 0;
    uint32_t due = 0;
    uint32_t end = 0;
    uint32_t index = 0;

    *full = 0;
    cd_masks(masks);
    cd_range(parameters, lba, lba + sectors - 1);
    cd_arm(command, sectors);
    cd_send(&log, command, parameters, 0);

    for (;;)
    {
        uint32_t now = tb_now();
        uint32_t time = now - log.start;
        uint8_t master = cd_master();
        uint8_t irq = cd_irq_line();

        if (master != last_master || irq != last_irq)
        {
            add_mark(marks, capacity, &count, full, time, MARK_MASTER, master, irq, 0, 0);
            last_master = master;
            last_irq = irq;
        }

        if ((master & CD_MASTER_SRQ) != 0)
        {
            uint8_t bytes[4];

            for (int i = 0; i < 4; i++)
                bytes[i] = in8(CD_COMMAND);

            add_mark(marks, capacity, &count, full, time, MARK_PACKET, bytes[0], bytes[1], cd_master(),
                cd_irq_line());
            out8(CD_MASTER, (uint8_t)(CD_CONTROL_SIRQ | masks));

            if (acks)
                add_mark(marks, capacity, &count, full, tb_now() - log.start, MARK_ACK, CD_CONTROL_SIRQ | masks, 0, 0,
                    0);

            if (bytes[0] == 0x22)
            {
                out8(CD_TRANSFER, 0x10);
                add_mark(marks, capacity, &count, full, tb_now() - log.start, MARK_DMA, 0, 0, 0, 0);
                dma = 1;

                uint32_t rise = tb_now();

                while ((cd_master() & CD_MASTER_DTS) == 0 && tb_now() - rise < 100)
                    ;
            }

            if ((bytes[0] == 0x06 || bytes[0] == 0x21) && end == 0)
                end = tb_now() - log.start + END_TAIL_US;

            continue;
        }

        if (dma && (cd_master() & CD_MASTER_DTS) == 0)
        {
            uint32_t found = cd_check_sector(CD_BUFFER + ((command & 0x1F) == CD_RAW_READ ? 4 : 0), lba + index, 0);
            add_mark(marks, capacity, &count, full, tb_now() - log.start, MARK_DMA_END, CD_PACKET_DMA,
                (uint8_t)(found - lba), cd_master(), cd_irq_line());
            dma = 0;
            index++;

            if (dei_delay != NO_ACK)
            {
                pending = 1;
                due = tb_now() + dei_delay;
            }
        }

        if (pending && (int32_t)(tb_now() - due) >= 0)
        {
            out8(CD_MASTER, (uint8_t)(CD_CONTROL_DEI | masks));

            if (acks)
                add_mark(marks, capacity, &count, full, tb_now() - log.start, MARK_ACK, CD_CONTROL_DEI | masks, 0, 0,
                    0);
            pending = 0;
        }

        if ((end != 0 && time >= end) || time >= TIMELINE_LIMIT)
            break;
    }

    out8(0x00AF, 0x0F);
    cd_masks(0);
    return count;
}

// From the DMA start to DTS falling; between checks the CPU stores 16 dwords to VRAM or RAM
static void step_dma(transfer_t* result)
{
    for (int target = 0; target < DMA_CASES; target++)
    {
        volatile uint32_t* stores = (volatile uint32_t*)(target == 1 ? g_machine.vram + VRAM_TARGET : RAM_TARGET);

        result->dma_low[target] = 0xFFFFFFFFu;

        for (int i = 0; i < DMA_REPEATS; i++)
        {
            cd_log_t log;
            uint32_t count = 0;

            shell_step("DMA %s %u/%u", k_dma_names[target], i + 1, DMA_REPEATS);

            if (!wait_ready(&log, CD_PAD_LBA + 60 + (uint32_t)i, CD_READ | CD_STATUS))
            {
                result->dma_failed[target]++;
                cd_quiet();
                continue;
            }

            uint32_t start = tb_now();
            out8(CD_TRANSFER, 0x10);

            while ((cd_master() & CD_MASTER_DTS) == 0 && tb_now() - start < 100)
                ;

            while ((cd_master() & CD_MASTER_DTS) != 0 && tb_now() - start < 100000u)
            {
                if (target != 0)
                {
                    for (int k = 0; k < 16; k++)
                        stores[k] = count;

                    count += 16;
                }
            }

            uint32_t time = tb_now() - start;

            if ((cd_master() & CD_MASTER_DTS) != 0)
                result->dma_failed[target]++;
            else
            {
                result->dma_low[target] = time < result->dma_low[target] ? time : result->dma_low[target];
                result->dma_high[target] = time > result->dma_high[target] ? time : result->dma_high[target];
                result->stores[target] = count;
            }

            cd_collect(&log, 1000000u, 100000u, 2000000u, CD_UNTIL_END);
            out8(0x00AF, 0x0F);
            cd_quiet();
        }
    }
}

// The read is sent and the collection stops at the first data ready, its DMA not started
static int wait_ready(cd_log_t* log, uint32_t lba, uint8_t command)
{
    uint8_t parameters[8];

    cd_range(parameters, lba, lba);
    cd_arm(command, 1);
    cd_send(log, command, parameters, 0);
    log->lba = lba;
    cd_collect(log, 8000000u, 3000000u, 8000000u, CD_HOLD_DMA | CD_UNTIL_END);
    return cd_find(log, 0x22, 0) != 0;
}

// SMIM and DEIM from the case's low bits, STATUS and IRQ in the command from the high ones
static void step_cases(transfer_t* result)
{
    for (int i = 0; i < CASES; i++)
    {
        case_t* entry = &result->cases[i];

        entry->masks = (uint8_t)(((i & 1) != 0 ? CD_CONTROL_SMIM : 0) | ((i & 2) != 0 ? CD_CONTROL_DEIM : 0));
        entry->command = (uint8_t)(CD_READ | ((i & 4) != 0 ? CD_STATUS : 0) | ((i & 8) != 0 ? CD_IRQ : 0));
        shell_step("IRQ CASE %02X MASKS %u", entry->command, entry->masks);
        entry->count = (uint8_t)timeline(entry->marks, CASE_MARKS, &entry->full, entry->command, CD_PAD_LBA + 70, 1,
            entry->masks, 0, 0);
        cd_quiet();
    }
}

// 04C0h and IRQ9: SIRQ set by 80h, after the packet is read, after a mask-only write, after the acknowledgement;
// DEI after a DMA end, after the 06h packet is read, after its acknowledgement; SIRQ when a command follows unacked
static void step_clears(transfer_t* result)
{
    static const uint8_t k_zero[8];
    cd_log_t log;

    shell_step("WHAT CLEARS SIRQ");
    cd_masks(CD_CONTROL_SMIM);
    cd_send(&log, CD_STATE | CD_STATUS | CD_IRQ, k_zero, 0);
    uint32_t start = tb_now();

    while ((cd_master() & CD_MASTER_SRQ) == 0 && tb_now() - start < 1000000u)
        ;

    tb_delay(1000);
    result->clears[0] = cd_master();
    result->clears_irq[0] = cd_irq_line();

    for (int i = 0; i < 4; i++)
        (void)in8(CD_COMMAND);

    result->clears[1] = cd_master();
    result->clears_irq[1] = cd_irq_line();
    out8(CD_MASTER, CD_CONTROL_SMIM);
    result->clears[2] = cd_master();
    result->clears_irq[2] = cd_irq_line();
    out8(CD_MASTER, CD_CONTROL_SIRQ | CD_CONTROL_SMIM);
    result->clears[3] = cd_master();
    result->clears_irq[3] = cd_irq_line();
    cd_quiet();

    shell_step("WHAT CLEARS DEI");
    cd_masks(CD_CONTROL_DEIM);

    if (wait_ready(&log, CD_PAD_LBA + 80, CD_READ | CD_STATUS))
    {
        cd_drain(&log, 0);
        result->clears[4] = cd_master();
        result->clears_irq[4] = cd_irq_line();
        start = tb_now();

        while ((cd_master() & CD_MASTER_SRQ) == 0 && tb_now() - start < 1000000u)
            ;

        for (int i = 0; i < 4; i++)
            (void)in8(CD_COMMAND);

        result->clears[5] = cd_master();
        result->clears_irq[5] = cd_irq_line();
        out8(CD_MASTER, CD_CONTROL_DEI | CD_CONTROL_DEIM);
        result->clears[6] = cd_master();
        result->clears_irq[6] = cd_irq_line();
    }

    out8(0x00AF, 0x0F);
    cd_quiet();

    shell_step("SIRQ, THEN A COMMAND");
    cd_masks(CD_CONTROL_SMIM);
    cd_send(&log, CD_STATE | CD_STATUS | CD_IRQ, k_zero, 0);
    start = tb_now();

    while ((cd_master() & CD_MASTER_SIRQ) == 0 && tb_now() - start < 1000000u)
        ;

    for (int i = 0; i < 8; i++)
        out8(CD_PARAMETER, 0);

    out8(CD_COMMAND, CD_STATE | CD_STATUS | CD_IRQ);
    result->clears[7] = cd_master();
    result->clears_irq[7] = cd_irq_line();
    cd_quiet();
}

static void step_deferred(transfer_t* result)
{
    for (int i = 0; i < DEFERRED; i++)
    {
        timeline_t* entry = &result->deferred[i];
        shell_step("DEI ACKED AFTER %u MS", k_deferred_us[i] / 1000);
        entry->masks = CD_CONTROL_SMIM | CD_CONTROL_DEIM;
        entry->command = CD_READ | CD_STATUS | CD_IRQ;
        entry->count = (uint8_t)timeline(entry->marks, MARKS, &entry->full, entry->command, CD_PAD_LBA + 90, 3,
            entry->masks, k_deferred_us[i], 1);
        cd_quiet();
    }
}

// Data ready left alone for a while: the packets meanwhile, then a late DMA, the LBA it carried and the rest
static void step_undrained(transfer_t* result)
{
    for (int i = 0; i < UNDRAINED; i++)
    {
        cd_log_t log;
        uint32_t lba = CD_PAD_LBA + 100 + (uint32_t)i * 4;

        shell_step("UNDRAINED %u MS", k_undrained_us[i] / 1000);
        result->undrained_lba[i] = CD_NO_LBA;

        if (wait_ready(&log, lba, CD_READ | CD_STATUS))
        {
            cd_poll(&log, k_undrained_us[i]);
            result->undrained_dma[i] = cd_drain(&log, 0);
            result->undrained_lba[i] = cd_check_sector(CD_BUFFER, lba, 0);
            cd_collect(&log, 1000000u, 300000u, 3000000u, CD_DMA | CD_UNTIL_END);
        }

        out8(0x00AF, 0x0F);
        cd_keep(&result->undrained[i], &log);
        cd_quiet();
    }
}

// A count of 4,095 bytes, so only the CDC ends the transfer; the count left gives the bytes it moved
static void step_sizes(transfer_t* result)
{
    for (int form = 0; form < FORMS; form++)
    {
        cd_log_t log;
        uint8_t parameters[8];
        uint8_t command = (uint8_t)(k_forms[form] | CD_STATUS);

        shell_step("SECTOR SIZE %02XH", command);
        memset((void*)CD_BUFFER, 0, PROBE_COUNT + 1);
        cd_range(parameters, CD_PAD_LBA + 130, CD_PAD_LBA + 130);
        cdc_dma_setup(3, CD_BUFFER, (uint16_t)PROBE_COUNT, 0x54);
        cd_send(&log, command, parameters, 0);
        cd_collect(&log, 8000000u, 3000000u, 8000000u, CD_HOLD_DMA | CD_UNTIL_END);

        if (cd_find(&log, 0x22, 0) == 0)
        {
            result->size_flags[form] = 1;
            cd_quiet();
            continue;
        }

        uint32_t start = tb_now();
        out8(CD_TRANSFER, 0x10);

        while ((cd_master() & CD_MASTER_DTS) == 0 && tb_now() - start < 100)
            ;

        while ((cd_master() & CD_MASTER_DTS) != 0 && tb_now() - start < 100000u)
            ;

        if ((cd_master() & CD_MASTER_DTS) != 0)
        {
            result->size_flags[form] = 2;
            cd_reset();
            tb_delay(10000);
        }

        result->sizes[form] = (uint16_t)(PROBE_COUNT - dma_current());
        memcpy(result->heads[form], (const void*)CD_BUFFER, HEAD_BYTES);
        out8(0x00AF, 0x0F);
        cd_quiet();
    }

    step_short(result);
}

// Then two sectors with a 1,024-byte count: whether the count's end finishes the sector and the next one comes
static void step_short(transfer_t* result)
{
    uint8_t parameters[8];
    cd_log_t log;

    shell_step("DMA COUNT 1024");
    cd_range(parameters, CD_PAD_LBA + 140, CD_PAD_LBA + 141);
    cdc_dma_setup(3, CD_BUFFER, 1023, 0x54);
    cd_send(&log, CD_READ | CD_STATUS, parameters, 0);
    log.lba = CD_PAD_LBA + 140;
    cd_collect(&log, 8000000u, 1000000u, 8000000u, CD_DMA | CD_UNTIL_END);
    out8(0x00AF, 0x0F);
    cd_keep(&result->short_count, &log);
    cd_quiet();
}

// 04C6h bit 3 instead of the DMA: 04C4h read until STS falls, at most 4,096 bytes
static void step_cpu(transfer_t* result)
{
    cd_log_t log;
    uint8_t* buffer = (uint8_t*)CD_BUFFER;
    uint32_t count = 0;

    shell_step("CPU TRANSFER");
    result->cpu_lba = CD_NO_LBA;

    if (wait_ready(&log, CD_PAD_LBA + 150, CD_READ | CD_STATUS))
    {
        out8(0x00AF, 0x0F);
        uint32_t start = tb_now();
        out8(CD_TRANSFER, 0x08);
        result->cpu_master[0] = cd_master();

        while ((cd_master() & CD_MASTER_STS) != 0 && count < 4096)
            buffer[count++] = in8(CD_PARAMETER);

        result->cpu_time = tb_now() - start;
        result->cpu_master[1] = cd_master();
        result->cpu_bytes = (uint16_t)count;
        result->cpu_lba = cd_check_sector(CD_BUFFER, CD_PAD_LBA + 150, 1);
        cd_collect(&log, 1000000u, 300000u, 3000000u, CD_UNTIL_END);
    }

    cd_keep(&result->cpu, &log);
    cd_quiet();
}

static uint16_t dma_current(void)
{
    out8(0x00A1, 0x03);
    uint8_t low = in8(0x00A2);
    uint8_t high = in8(0x00A3);
    return (uint16_t)(low | (high << 8));
}

static int draw_marks(int row, const mark_t* marks, int count, int first)
{
    static const char* const k_kinds[] = { "04C0", "PKT ", "DMA ", "END ", "ACK " };

    for (int i = first; i < count && row <= SHELL_BODY_BOTTOM; i++)
    {
        const mark_t* mark = &marks[i];
        uint32_t time = mark->stamp >> 8;
        uint8_t kind = (uint8_t)mark->stamp;

        if (kind == MARK_MASTER)
            shell_line(row++, COLOR_WHITE, "%5u.%u %s %02X IRQ %u", time / 1000, time / 100 % 10, k_kinds[kind],
                mark->bytes[0], mark->bytes[1]);
        else if (kind == MARK_PACKET || kind == MARK_DMA_END)
            shell_line(row++, COLOR_CYAN, "%5u.%u %s %02X%02X >%02X IRQ %u", time / 1000, time / 100 % 10,
                k_kinds[kind], mark->bytes[0], mark->bytes[1], mark->bytes[2], mark->bytes[3]);
        else
            shell_line(row++, COLOR_GRAY, "%5u.%u %s %02X", time / 1000, time / 100 % 10, k_kinds[kind],
                mark->bytes[0]);
    }

    return row;
}

// The IRQ9 line right after the case's first packet starting with first (or the DMA end), before its acknowledgement
static const char* event_irq(const case_t* entry, uint8_t first)
{
    for (int i = 0; i < entry->count; i++)
    {
        const mark_t* mark = &entry->marks[i];
        uint8_t kind = (uint8_t)mark->stamp;

        if ((kind == MARK_PACKET || kind == MARK_DMA_END) && mark->bytes[0] == first)
            return mark->bytes[3] != 0 ? "I" : (mark->bytes[2] & (CD_MASTER_SIRQ | CD_MASTER_DEI)) != 0 ? "F" : "-";
    }

    return ".";
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 12;
}

static void page(const void* data, uint32_t steps, int number)
{
    const transfer_t* result = (const transfer_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number <= 2)
    {
        const timeline_t* entry = &result->timelines[number == 0 ? 0 : 1];
        int first = number == 2 ? 21 : 0;

        shell_line(row++, COLOR_CYAN, "%u SECTOR%s, CMD %02X, MASKS %u, MS%s", number == 0 ? 1 : 3,
            number == 0 ? "" : "S", entry->command, entry->masks, number == 2 ? ", CONT" : "");
        row = draw_marks(row, entry->marks, entry->count, first);

        if (entry->full)
            shell_line(row++, COLOR_YELLOW, "TIMELINE FULL");

        return;
    }

    if (number == 3)
    {
        shell_line(row++, COLOR_CYAN, "SECTOR DMA, US FROM START TO DTS 0");

        for (int i = 0; i < DMA_CASES; i++)
            shell_line(row++, result->dma_failed[i] != 0 ? COLOR_YELLOW : COLOR_WHITE, "%-11s %5u-%-5u F%u",
                k_dma_names[i], result->dma_low[i] == 0xFFFFFFFFu ? 0 : result->dma_low[i], result->dma_high[i],
                result->dma_failed[i]);

        if (result->dma_low[0] != 0 && result->dma_low[0] != 0xFFFFFFFFu)
            shell_line(row++, COLOR_GREEN, "%u KB/S, %u NS A BYTE (BOOK 2000)",
                2048u * 1000u / result->dma_low[0], result->dma_low[0] * 1000u / 2048u);

        shell_line(row++, COLOR_WHITE, "STORES DURING DMA: %u VRAM %u RAM", result->stores[1], result->stores[2]);
        row++;
        shell_line(row++, COLOR_CYAN, "SECTOR SIZE BY DMA COUNT");

        for (int i = 0; i < FORMS; i++)
        {
            const uint8_t* head = result->heads[i];
            shell_line(row++, result->size_flags[i] != 0 ? COLOR_YELLOW : COLOR_WHITE, "%02XH %4u BYTES %s",
                k_forms[i] | CD_STATUS, result->sizes[i], result->size_flags[i] == 1 ? "NO DATA" :
                result->size_flags[i] == 2 ? "NEVER ENDED" : "");
            shell_line(row++, COLOR_GRAY, " %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
                head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7], head[8], head[9], head[10],
                head[11], head[12], head[13], head[14], head[15]);
        }

        shell_line(row++, COLOR_GRAY, "MODE 1 2048, RAW 2340, MODE 2 2336");
        return;
    }

    if (number == 4)
    {
        shell_line(row++, COLOR_CYAN, "IRQ9 AFTER EACH EVENT");
        shell_line(row++, COLOR_GRAY, "I LINE UP, F FLAG ONLY, - NONE");
        shell_line(row++, COLOR_GRAY, "CMD  SMIM DEIM  ACC 22H DMA 06H");

        for (int i = 0; i < CASES; i++)
        {
            const case_t* entry = &result->cases[i];
            shell_line(row++, entry->full ? COLOR_YELLOW : COLOR_WHITE, "%02X    %u    %u     %s   %s   %s   %s",
                entry->command, (entry->masks & CD_CONTROL_SMIM) != 0, (entry->masks & CD_CONTROL_DEIM) != 0,
                event_irq(entry, 0x00), event_irq(entry, 0x22), event_irq(entry, CD_PACKET_DMA),
                event_irq(entry, 0x06));
        }

        shell_line(row++, COLOR_GRAY, "BOOK: IRQ = SIRQ.SMIM OR DEI.DEIM");
        return;
    }

    if (number == 5)
    {
        static const char* const k_clears[8] =
        {
            "SIRQ SET BY 80H", "PACKET READ", "MASK-ONLY WRITE", "ACK BIT 7", "DEI AFTER DMA END",
            "06H PACKET READ", "ACK BIT 6", "NEXT CMD, NO ACK"
        };

        shell_line(row++, COLOR_CYAN, "04C0H AND IRQ9 AFTER");

        for (int i = 0; i < 8; i++)
            shell_line(row++, COLOR_WHITE, "%-18s %02X IRQ %u", k_clears[i], result->clears[i], result->clears_irq[i]);

        row++;
        return;
    }

    if (number >= 6 && number <= 8)
    {
        int index = number == 6 ? 1 : number == 7 ? 2 : 3;
        const timeline_t* entry = &result->deferred[index];
        int shown = 0;

        if (number == 6)
        {
            shell_line(row++, COLOR_CYAN, "DEI ACKED 0 MS AFTER: 22H AT");

            for (int i = 0; i < result->deferred[0].count; i++)
            {
                const mark_t* mark = &result->deferred[0].marks[i];

                if ((uint8_t)mark->stamp == MARK_PACKET && mark->bytes[0] == 0x22)
                    shell_line(row++, COLOR_WHITE, " %5u.%u MS", (mark->stamp >> 8) / 1000,
                        (mark->stamp >> 8) / 100 % 10);
            }
        }

        shell_line(row++, COLOR_CYAN, "DEI ACKED %u MS AFTER EACH DMA END", k_deferred_us[index] / 1000);

        for (int i = 0; i < entry->count && row <= SHELL_BODY_BOTTOM; i++)
        {
            const mark_t* mark = &entry->marks[i];
            uint8_t kind = (uint8_t)mark->stamp;

            if (kind == MARK_MASTER || kind == MARK_DMA)
                continue;

            row = draw_marks(row, mark, 1, 0);
            shown++;
        }

        if (shown == 0)
            shell_line(row++, COLOR_GRAY, "NOT RUN");


        return;
    }

    if (number == 9)
    {
        shell_line(row++, COLOR_CYAN, "UNDRAINED: MS, LATE DMA LBA");

        for (int i = 0; i < UNDRAINED && row <= SHELL_BODY_BOTTOM - 2; i++)
        {
            char label[24];
            const cd_keep_t* keep = &result->undrained[i];
            fmt_format(label, sizeof(label), "%u MS, DMA %s%u", k_undrained_us[i] / 1000,
                result->undrained_lba[i] == CD_NO_LBA ? "NONE " : "", result->undrained_lba[i] == CD_NO_LBA ? 0 :
                result->undrained_lba[i]);
            shell_line(row++, COLOR_CYAN, "%s", label);
            row = cd_draw_packets(row, keep->packets, keep->count > 4 ? 4 : keep->count);
        }

        return;
    }

    if (number == 10)
    {
        row = cd_draw_keep(row, "3 SECTORS", &result->multi);
        row++;
        row = cd_draw_keep(row, "2 SECTORS, COUNT 1024", &result->short_count);
        return;
    }

    shell_line(row++, COLOR_CYAN, "CPU TRANSFER THROUGH 04C4H");
    shell_line(row++, COLOR_WHITE, "04C0H AFTER 04C6H=08H: %02X", result->cpu_master[0]);
    shell_line(row++, COLOR_WHITE, "%u BYTES IN %u US, 04C0H %02X", result->cpu_bytes, result->cpu_time,
        result->cpu_master[1]);
    shell_line(row++, result->cpu_lba == CD_PAD_LBA + 150 ? COLOR_GREEN : COLOR_YELLOW, "DATA %s",
        result->cpu_lba == CD_PAD_LBA + 150 ? "OK" : "WRONG OR NONE");
    row = cd_draw_keep(row, "PACKETS", &result->cpu);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const transfer_t* result = (const transfer_t*)data;
    (void)steps;

    fmt_format(line, 34, "DMA %u US SIZES %u %u %u", result->dma_low[0] == 0xFFFFFFFFu ? 0 : result->dma_low[0],
        result->sizes[0], result->sizes[1], result->sizes[2]);
    *color = COLOR_WHITE;
}
