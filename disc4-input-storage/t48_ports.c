#include "tests.h"

// 4.8 Port timing: 4,096 accesses of each port in unrolled runs of 16, so code fetch drops out, the fastest of four,
// for the ports of this disc's devices that read and write without side effects. The empty loop's own time is shown,
// not taken off

#define PORTS 18
#define PASSES 256
#define ACCESSES (PASSES * 16)
#define REPEATS 4
#define READ 0x01
#define WRITE 0x02
#define TIME_BASE_READS 1000

typedef struct
{
    uint16_t port;
    uint8_t access;
    uint8_t value;
} port_case_t;

typedef struct
{
    uint32_t empty_us;
    uint32_t time_base_us;
    uint32_t read_us[PORTS];
    uint32_t write_us[PORTS];
} ports_t;

void ports_reads(uint32_t port, uint32_t passes);
void ports_writes(uint32_t port, uint32_t value, uint32_t passes);
void ports_empty(uint32_t passes);

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static uint32_t tenths(uint32_t microseconds);

static const port_case_t k_ports[PORTS] =
{
    { 0x0200, READ, 0 }, { 0x0202, READ | WRITE, 0x00 }, { 0x0204, READ | WRITE, 0x01 }, { 0x0206, READ | WRITE, 0x00 },
    { 0x0208, READ | WRITE, 0x00 }, { 0x020C, WRITE, 0x00 }, { 0x020D, READ, 0 }, { 0x020E, READ, 0 },
    { 0x3000, READ, 0 }, { 0x0600, READ, 0 }, { 0x0604, READ | WRITE, 0x00 }, { 0x04D2, READ, 0 },
    { 0x04D6, READ | WRITE, 0x0F }, { 0x0C32, READ, 0 }, { 0x0034, READ, 0 }, { 0x00A1, READ, 0 },
    { 0x00AB, READ, 0 }, { 0x0480, READ, 0 }
};

const test_t g_test_ports =
{
    "4.8", "PORT TIMING", 0, TEST_RUN_ALL, 1, sizeof(ports_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    ports_t* result = (ports_t*)data;
    (void)steps;

    result->empty_us = 0xFFFFFFFFu;

    for (int k = 0; k < REPEATS; k++)
    {
        uint32_t start = tb_now();
        ports_empty(PASSES);
        uint32_t elapsed = tb_now() - start;
        result->empty_us = elapsed < result->empty_us ? elapsed : result->empty_us;
    }

    uint32_t start = tb_now();

    for (int i = 0; i < TIME_BASE_READS; i++)
        (void)tb_now();

    result->time_base_us = tb_now() - start;

    for (int i = 0; i < PORTS; i++)
    {
        const port_case_t* entry = &k_ports[i];

        shell_step("PORT %04XH", entry->port);
        result->read_us[i] = 0xFFFFFFFFu;
        result->write_us[i] = 0xFFFFFFFFu;

        for (int k = 0; k < REPEATS; k++)
        {
            uint32_t start;
            uint32_t elapsed;

            if ((entry->access & READ) != 0)
            {
                start = tb_now();
                ports_reads(entry->port, PASSES);
                elapsed = tb_now() - start;
                result->read_us[i] = elapsed < result->read_us[i] ? elapsed : result->read_us[i];
            }

            if ((entry->access & WRITE) != 0)
            {
                start = tb_now();
                ports_writes(entry->port, entry->value, PASSES);
                elapsed = tb_now() - start;
                result->write_us[i] = elapsed < result->write_us[i] ? elapsed : result->write_us[i];
            }
        }

        if ((entry->access & READ) == 0)
            result->read_us[i] = 0;

        if ((entry->access & WRITE) == 0)
            result->write_us[i] = 0;
    }

    out8(0x04D6, 0x0F);
    out8(0x0208, 0x00);
    out8(0x020C, 0x00);
    out8(0x0604, 0x00);
}

// Tenths of a nanosecond an access
static uint32_t tenths(uint32_t microseconds)
{
    return microseconds * 10000u / ACCESSES;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 1;
}

static void page(const void* data, uint32_t steps, int number)
{
    const ports_t* result = (const ports_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;
    (void)number;

    shell_line(row++, COLOR_CYAN, "NS AN ACCESS, 16 IN A RUN");
    shell_line(row++, COLOR_GRAY, "PORT     READ    WRITE");

    for (int i = 0; i < PORTS; i++)
    {
        char read[12];
        char write[12];
        uint32_t r = tenths(result->read_us[i]);
        uint32_t w = tenths(result->write_us[i]);

        if (result->read_us[i] != 0)
            fmt_format(read, sizeof(read), "%5u.%u", r / 10, r % 10);
        else
            fmt_copy(read, "      -", sizeof(read));

        if (result->write_us[i] != 0)
            fmt_format(write, sizeof(write), "%5u.%u", w / 10, w % 10);
        else
            fmt_copy(write, "      -", sizeof(write));

        shell_line(row++, COLOR_WHITE, "%04XH  %s  %s", k_ports[i].port, read, write);
    }

    shell_line(row++, COLOR_GRAY, "EMPTY LOOP %u US FOR %u PASSES", result->empty_us, PASSES);
    shell_line(row++, COLOR_GRAY, "TIME BASE READ %u.%u US", result->time_base_us / TIME_BASE_READS,
        result->time_base_us / (TIME_BASE_READS / 10) % 10);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const ports_t* result = (const ports_t*)data;
    uint32_t fdc = tenths(result->read_us[0]);
    uint32_t cmos = tenths(result->read_us[8]);
    (void)steps;

    *color = COLOR_WHITE;
    fmt_format(line, 34, "0200H %u.%u 3000H %u.%u NS", fdc / 10, fdc % 10, cmos / 10, cmos % 10);
}
