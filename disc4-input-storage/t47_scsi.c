#include "tests.h"

// 4.7 SCSI interface (docs/12, HT-45), only where 0C32h doesn't read FFh. Read-only commands by programmed I/O (TEST
// UNIT READY, INQUIRY, READ CAPACITY), never a write and never a bus reset. Selection as the system ROM does it: the
// host's and the target's ID bits on the data port, then WEN and SEL, SEL dropped once BUSY rises. Each access of
// 0C30h answers one REQ. IRQ8 stays masked at the PIC; the slave's IRR shows the INT line

#define SCSI_DATA 0x0C30
#define SCSI_STATUS 0x0C32
#define WEN 0x80
#define IMSK 0x40
#define SEL 0x04
#define REQ 0x80
#define BUSY 0x08
#define INT 0x02
#define PHASE_MASK 0x70
#define IDS 7
#define PHASES 24
#define COMMANDS 3
#define INQUIRY_BYTES 36
#define SELECT_TIMEOUT_US 250000u
#define COMMAND_TIMEOUT_US 2000000u
#define FREE_TIMEOUT_US 100000u

typedef struct
{
    uint8_t status;
    uint8_t irr;
    uint16_t reserved;
    uint32_t time;
} scsi_phase_t;

typedef struct
{
    uint8_t phase_count;
    uint8_t result;
    uint8_t status_byte;
    uint8_t message;
    uint8_t data_count;
    uint8_t reserved[3];
    uint8_t data[INQUIRY_BYTES];
    scsi_phase_t phases[PHASES];
} scsi_command_t;

typedef struct
{
    uint8_t present;
    uint8_t devices;
    uint8_t target;
    uint8_t wen_busy;
    uint8_t passive[6];
    uint8_t worddma[4];
    uint32_t select_time[IDS];
    scsi_command_t commands[COMMANDS];
    scsi_command_t imsk[2];
} scsi_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static int bus_free(void);
static uint32_t select_target(int id, uint8_t control);
static void execute(scsi_command_t* log, int id, const uint8_t* cdb, int length, uint8_t control);
static const char* phase_name(uint8_t status);

static const uint8_t k_cdbs[COMMANDS][10] =
{
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x12, 0x00, 0x00, 0x00, INQUIRY_BYTES, 0x00 },
    { 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
};

static const int k_cdb_lengths[COMMANDS] = { 6, 6, 10 };
static const char* const k_command_names[COMMANDS] = { "TEST UNIT READY", "INQUIRY", "READ CAPACITY" };

const test_t g_test_scsi =
{
    "4.7", "SCSI INTERFACE", "SCSI", TEST_RUN_ALL, 1, sizeof(scsi_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    scsi_t* result = (scsi_t*)data;
    (void)steps;

    result->passive[0] = in8(SCSI_STATUS);
    result->passive[1] = in8(SCSI_STATUS);
    result->passive[2] = in8(0x0C34);
    result->passive[3] = in8(0x0034);
    result->target = 0xFF;

    if (result->passive[0] == 0xFF && result->passive[1] == 0xFF)
        return;

    result->present = 1;
    result->passive[4] = in8(SCSI_DATA);
    result->passive[5] = in8(SCSI_STATUS);
    pic_mask(8);

    for (int id = 0; id < IDS; id++)
    {
        shell_step("SELECTING ID %u", id);
        result->select_time[id] = select_target(id, WEN);

        if (result->select_time[id] != 0)
        {
            result->devices |= (uint8_t)(1 << id);

            if (result->target == 0xFF)
                result->target = (uint8_t)id;

            uint32_t start = tb_now();

            while ((in8(SCSI_STATUS) & BUSY) != 0 && tb_now() - start < COMMAND_TIMEOUT_US)
            {
                if ((in8(SCSI_STATUS) & REQ) != 0)
                {
                    uint8_t status = in8(SCSI_STATUS);

                    if ((status & 0x40) != 0)
                        (void)in8(SCSI_DATA);
                    else
                        out8(SCSI_DATA, 0x00);
                }
            }
        }
    }

    if (result->target != 0xFF)
    {
        result->worddma[0] = in8(0x0034);
        result->worddma[1] = in8(0x0C34);

        for (int i = 0; i < COMMANDS; i++)
        {
            shell_step("%s", k_command_names[i]);
            execute(&result->commands[i], result->target, k_cdbs[i], k_cdb_lengths[i], WEN);
            tb_delay(10000u);
        }

        result->worddma[2] = in8(0x0034);
        result->worddma[3] = in8(0x0C34);

        for (int imsk = 0; imsk < 2; imsk++)
        {
            shell_step("IMSK %u", imsk);
            execute(&result->imsk[imsk], result->target, k_cdbs[0], 6, (uint8_t)(WEN | (imsk ? IMSK : 0)));
            tb_delay(10000u);
        }

        shell_step("SELECTION WITH WEN 0");
        bus_free();
        out8(SCSI_DATA, (uint8_t)(0x80 | (1 << result->target)));
        out8(SCSI_STATUS, SEL);
        uint32_t start = tb_now();

        while (tb_now() - start < SELECT_TIMEOUT_US && (in8(SCSI_STATUS) & BUSY) == 0)
            ;

        result->wen_busy = (in8(SCSI_STATUS) & BUSY) != 0;
        out8(SCSI_STATUS, 0x00);

        if (result->wen_busy)
        {
            scsi_command_t drop;
            memset(&drop, 0, sizeof(drop));
            execute(&drop, -1, k_cdbs[0], 6, WEN);
        }
    }

    out8(SCSI_STATUS, WEN);
    pic_eoi(8);
}

static int bus_free(void)
{
    uint32_t start = tb_now();

    while ((in8(SCSI_STATUS) & BUSY) != 0)
    {
        if (tb_now() - start > FREE_TIMEOUT_US)
            return 0;
    }

    return 1;
}

// The time from SEL to BUSY, 0 when the target never answered
static uint32_t select_target(int id, uint8_t control)
{
    uint32_t start;
    uint32_t time = 0;

    if (!bus_free())
        return 0;

    out8(SCSI_DATA, (uint8_t)(0x80 | (1 << id)));
    out8(SCSI_STATUS, (uint8_t)(control | SEL));
    start = tb_now();

    while (tb_now() - start < SELECT_TIMEOUT_US)
    {
        if ((in8(SCSI_STATUS) & BUSY) != 0)
        {
            time = tb_now() - start;
            time = time != 0 ? time : 1;
            break;
        }
    }

    out8(SCSI_STATUS, control);
    return time;
}

// Selection (skipped with id -1, the target already selected), then every phase served until BUSY drops; each
// change of the status register is logged with the slave IRR
static void execute(scsi_command_t* log, int id, const uint8_t* cdb, int length, uint8_t control)
{
    uint32_t start;
    uint8_t last = 0xFF;
    int sent = 0;

    log->result = 0;

    if (id >= 0 && select_target(id, control) == 0)
    {
        log->result = 1;
        return;
    }

    start = tb_now();

    while ((in8(SCSI_STATUS) & BUSY) != 0)
    {
        uint8_t status = in8(SCSI_STATUS);

        if (tb_now() - start > COMMAND_TIMEOUT_US)
        {
            log->result = 2;
            break;
        }

        if (status != last && log->phase_count < PHASES)
        {
            scsi_phase_t* phase = &log->phases[log->phase_count++];
            phase->status = status;
            phase->irr = (uint8_t)(pic_read_irr() >> 8);
            phase->time = tb_now() - start;
        }

        last = status;

        if ((status & REQ) == 0)
            continue;

        switch (status & PHASE_MASK)
        {
            case 0x10:
                out8(SCSI_DATA, sent < length ? cdb[sent] : 0x00);
                sent++;
                break;
            case 0x40:
            {
                uint8_t value = in8(SCSI_DATA);

                if (log->data_count < INQUIRY_BYTES)
                    log->data[log->data_count++] = value;

                break;
            }
            case 0x50:
                log->status_byte = in8(SCSI_DATA);
                break;
            case 0x70:
                log->message = in8(SCSI_DATA);
                break;
            case 0x30:
                out8(SCSI_DATA, 0x08);
                break;
            default:
                out8(SCSI_DATA, 0x00);
                break;
        }
    }
}

static const char* phase_name(uint8_t status)
{
    switch (status & PHASE_MASK)
    {
        case 0x00:
            return "DATA OUT";
        case 0x40:
            return "DATA IN";
        case 0x10:
            return "COMMAND";
        case 0x50:
            return "STATUS";
        case 0x30:
            return "MSG OUT";
        case 0x70:
            return "MSG IN";
        default:
            return "RESERVED";
    }
}

static int pages(const void* data, uint32_t steps)
{
    const scsi_t* result = (const scsi_t*)data;
    (void)steps;
    return result->present && result->target != 0xFF ? 3 : 1;
}

static void page(const void* data, uint32_t steps, int number)
{
    const scsi_t* result = (const scsi_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "SCSI INTERFACE");
        shell_line(row++, COLOR_WHITE, "0C32H %02X %02X, 0C34H %02X, 0034H %02X", result->passive[0],
            result->passive[1], result->passive[2], result->passive[3]);

        if (!result->present)
        {
            shell_line(row++, COLOR_GREEN, "NO INTERFACE (0C32H READS FFH)");
            return;
        }

        shell_line(row++, COLOR_WHITE, "IDLE 0C30H %02X, THEN 0C32H %02X", result->passive[4], result->passive[5]);
        shell_line(row++, COLOR_WHITE, "EX (0C32H BIT 2) %u", (result->passive[0] >> 2) & 1);
        row++;
        shell_line(row++, COLOR_CYAN, "SELECTION, US TO BUSY");

        for (int id = 0; id < IDS; id++)
        {
            if (result->select_time[id] != 0)
                shell_line(row++, COLOR_WHITE, "ID %u  ANSWERED IN %u US", id, result->select_time[id]);
            else
                shell_line(row++, COLOR_WHITE, "ID %u  TIMED OUT", id);
        }

        if (result->target != 0xFF)
        {
            row++;
            shell_line(row++, COLOR_WHITE, "WORDDMA 0034H %02X>%02X 0C34H %02X>%02X", result->worddma[0],
                result->worddma[2], result->worddma[1], result->worddma[3]);
            shell_line(row++, COLOR_GREEN, "WEN 0 BLOCKS SELECTION: %s", result->wen_busy ? "NO" : "YES");
        }

        return;
    }

    if (number == 1)
    {
        const scsi_command_t* inquiry = &result->commands[1];
        const scsi_command_t* capacity = &result->commands[2];
        char text[29];

        shell_line(row++, COLOR_CYAN, "ID %u COMMANDS: STATUS, MESSAGE", result->target);

        for (int i = 0; i < COMMANDS; i++)
            shell_line(row++, COLOR_WHITE, "%-15s %02X %02X %u PH %s", k_command_names[i],
                result->commands[i].status_byte, result->commands[i].message, result->commands[i].phase_count,
                result->commands[i].result == 0 ? "OK" : result->commands[i].result == 1 ? "NO SEL" : "TIMEOUT");

        for (int i = 0; i < 28; i++)
        {
            uint8_t c = inquiry->data[8 + i];
            text[i] = c >= 0x20 && c < 0x7F ? (char)c : '.';
        }

        text[28] = 0;
        row++;
        shell_line(row++, COLOR_WHITE, "INQUIRY TYPE %02X, %u BYTES", inquiry->data[0], inquiry->data_count);
        shell_line(row++, COLOR_WHITE, "%s", text);
        shell_line(row++, COLOR_WHITE, "CAPACITY %02X%02X%02X%02X BLOCK %02X%02X%02X%02X", capacity->data[0],
            capacity->data[1], capacity->data[2], capacity->data[3], capacity->data[4], capacity->data[5],
            capacity->data[6], capacity->data[7]);
        row++;
        shell_line(row++, COLOR_CYAN, "INQUIRY PHASES: STATUS, IRR, US");

        for (int i = 0; i < inquiry->phase_count && row <= SHELL_BODY_BOTTOM; i++)
            shell_line(row++, COLOR_WHITE, "%02X %-8s %02X %6u", inquiry->phases[i].status,
                phase_name(inquiry->phases[i].status), inquiry->phases[i].irr, inquiry->phases[i].time);

        return;
    }

    shell_line(row++, COLOR_CYAN, "TEST UNIT READY, IMSK 0 AND 1");

    for (int imsk = 0; imsk < 2; imsk++)
    {
        const scsi_command_t* log = &result->imsk[imsk];
        int int_seen = 0;
        int irq_seen = 0;

        for (int i = 0; i < log->phase_count; i++)
        {
            int_seen |= (log->phases[i].status & INT) != 0;
            irq_seen |= (log->phases[i].irr & 0x01) != 0;
        }

        shell_line(row++, COLOR_GREEN, "IMSK %u: INT %s, IRQ8 %s", imsk, int_seen ? "SHOWN" : "NEVER",
            irq_seen ? "REQUESTED" : "NEVER");

        for (int i = 0; i < log->phase_count && i < 8; i++)
            shell_line(row++, COLOR_WHITE, " %02X %-8s IRR %02X %6u", log->phases[i].status,
                phase_name(log->phases[i].status), log->phases[i].irr, log->phases[i].time);
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const scsi_t* result = (const scsi_t*)data;
    (void)steps;

    *color = COLOR_WHITE;

    if (!result->present)
        fmt_copy(line, "NO INTERFACE", 34);
    else
        fmt_format(line, 34, "DEVICES %02X, EX %u", result->devices, (result->passive[0] >> 2) & 1);
}
