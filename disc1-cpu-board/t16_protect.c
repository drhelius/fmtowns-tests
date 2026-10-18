#include "tests.h"
#include "cdc.h"

// 1.6 Write protect and expansion NMI (docs/12, HT-21, HT-22): which of 00000h-0001Fh 0020h WRPROT keeps, for byte,
// straddling and DMA writes, and how 05C0h/05C2h read and latch

#define VECTORS 32
#define STRADDLE_FIRST 4
#define STRADDLE_COUNT 9
#define SECTOR_LBA 1

typedef struct
{
    uint8_t pattern[VECTORS];
    uint8_t map_88[VECTORS];
    uint8_t map_80[VECTORS];
    uint32_t straddle_word[STRADDLE_COUNT];
    uint32_t straddle_dword[STRADDLE_COUNT];
    uint8_t dma_after[VECTORS];
    uint8_t dma_sector[VECTORS];
    cdc_read_t dma;
    uint8_t read_0020;
    uint8_t boot_05c0;
    uint8_t boot_05c2;
    uint8_t port_0028;
    uint8_t written[3];
    uint8_t read_05c0[3];
    uint8_t read_05c2[3];
    uint8_t restored_05c0;
    uint8_t reserved[2];
} protect_t;

static const uint8_t k_writes[3] = { 0x00, 0x08, 0xF7 };

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void fill(const uint8_t* pattern);
static void byte_map(const uint8_t* pattern, uint8_t control, uint8_t* map);
static uint32_t changed_mask(const uint8_t* pattern);
static int verdict_range(const protect_t* result);
static int verdict_straddle(const protect_t* result);
static int verdict_dma(const protect_t* result);
static int verdict_undefined(const protect_t* result);

const test_t g_test_protect =
{
    "1.6", "WRITE PROTECT, 05C0H", 0, TEST_RUN_ALL, 1, sizeof(protect_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    protect_t* result = (protect_t*)data;
    (void)steps;

    for (int i = 0; i < VECTORS; i++)
        result->pattern[i] = (uint8_t)(0x40 + i);

    shell_step("BYTE MAP 88H");
    byte_map(result->pattern, 0x88, result->map_88);
    shell_step("BYTE MAP 80H");
    byte_map(result->pattern, 0x80, result->map_80);

    shell_step("STRADDLING WRITES");

    for (int i = 0; i < STRADDLE_COUNT; i++)
    {
        uint32_t address = STRADDLE_FIRST + (uint32_t)i;
        fill(result->pattern);
        out8(0x0020, 0x88);
        MMIO16(address) = 0xA5A5;
        result->straddle_word[i] = changed_mask(result->pattern);
        fill(result->pattern);
        out8(0x0020, 0x88);
        MMIO32(address) = 0xA5A5A5A5u;
        result->straddle_dword[i] = changed_mask(result->pattern);
    }

    shell_step("CDC DMA INTO 00000H");
    fill(result->pattern);
    out8(0x0020, 0x88);
    result->read_0020 = in8(0x0020);
    cdc_read_dma(SECTOR_LBA, 0x00000000u, &result->dma);

    if (result->dma.result == CDC_TIMEOUT)
        shell_set_status(RECORD_STATUS_TIMEOUT);

    for (int i = 0; i < VECTORS; i++)
    {
        result->dma_after[i] = MMIO8(i);
        result->dma_sector[i] = MMIO8(0x00080000u + i);
    }

    out8(0x0020, 0x00);

    shell_step("05C0H AND 05C2H");
    result->boot_05c0 = g_boot.port_05c0;
    result->boot_05c2 = g_boot.port_05c2;
    result->port_0028 = in8(0x0028);

    if (result->port_0028 != 0xFF)
        out8(0x0028, result->port_0028 | 0x01);

    uint8_t saved = in8(0x05C0);

    for (int i = 0; i < 3; i++)
    {
        result->written[i] = k_writes[i];
        out8(0x05C0, k_writes[i]);
        result->read_05c0[i] = in8(0x05C0);
        result->read_05c2[i] = in8(0x05C2);
    }

    out8(0x05C0, saved & 0x08);
    result->restored_05c0 = in8(0x05C0);

    if (result->port_0028 != 0xFF)
        out8(0x0028, result->port_0028);
}

static void fill(const uint8_t* pattern)
{
    out8(0x0020, 0x00);

    for (int i = 0; i < VECTORS; i++)
        MMIO8(i) = pattern[i];
}

// Every byte written alone with WRPROT set, then all 32 read back
static void byte_map(const uint8_t* pattern, uint8_t control, uint8_t* map)
{
    fill(pattern);
    out8(0x0020, control);

    for (int i = 0; i < VECTORS; i++)
        MMIO8(i) = (uint8_t)~pattern[i];

    for (int i = 0; i < VECTORS; i++)
        map[i] = MMIO8(i);

    out8(0x0020, 0x00);
}

static uint32_t changed_mask(const uint8_t* pattern)
{
    uint32_t mask = 0;

    for (int i = 0; i < VECTORS; i++)
    {
        if (MMIO8(i) != pattern[i])
            mask |= 1u << i;
    }

    out8(0x0020, 0x00);
    return mask;
}

// 0 only 00008h-0000Bh kept, 1 the aligned dword or more of the table, 2 nothing kept, 3 another set
static int verdict_range(const protect_t* result)
{
    int kept = 0;
    int only_nmi = 1;

    for (int i = 0; i < VECTORS; i++)
    {
        int protected_byte = result->map_88[i] == result->pattern[i];
        kept += protected_byte;

        if (protected_byte != (i >= 8 && i <= 11))
            only_nmi = 0;
    }

    if (kept == 0)
        return 2;

    if (only_nmi)
        return 0;

    return kept == VECTORS ? 1 : 3;
}

// A word at 00007h covers 07h and 08h: 0 its unprotected byte lands, 1 the whole write is dropped
static int verdict_straddle(const protect_t* result)
{
    uint32_t mask = result->straddle_word[7 - STRADDLE_FIRST];
    return (mask & 0x80) != 0 ? 0 : 1;
}

static int verdict_dma(const protect_t* result)
{
    for (int i = 8; i < 12; i++)
    {
        if (result->dma_after[i] != result->pattern[i])
            return 1;
    }

    return 0;
}

// 0 the other bits of 05C0h read 1, 1 they read 0, 2 they follow the writes
static int verdict_undefined(const protect_t* result)
{
    uint8_t after_zero = result->read_05c0[0] & 0xF7;
    uint8_t after_ones = result->read_05c0[2] & 0xF7;

    if (after_zero == 0xF7 && after_ones == 0xF7)
        return 0;

    if (after_zero == 0 && after_ones == 0)
        return 1;

    return 2;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 2;
}

static void page(const void* data, uint32_t steps, int number)
{
    static const char* const k_range[] = { "RANGE 00008-0000B", "RANGE 00000-0001F+", "NOTHING KEPT", "OTHER RANGE" };
    static const char* const k_straddle[] = { "STRADDLE PARTIAL", "STRADDLE WHOLE" };
    static const char* const k_dma[] = { "DMA BLOCKED", "DMA PASSES" };
    static const char* const k_undefined[] = { "UNDEFINED BITS 1", "UNDEFINED BITS 0", "BITS LATCH" };
    const protect_t* result = (const protect_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        char map88[VECTORS + 1];
        char map80[VECTORS + 1];
        char dma[VECTORS + 1];

        for (int i = 0; i < VECTORS; i++)
        {
            map88[i] = result->map_88[i] == result->pattern[i] ? 'K' : '.';
            map80[i] = result->map_80[i] == result->pattern[i] ? 'K' : '.';
            dma[i] = result->dma_after[i] == result->pattern[i] ? 'K' :
                result->dma_after[i] == result->dma_sector[i] ? '.' : '?';
        }

        map88[VECTORS] = map80[VECTORS] = dma[VECTORS] = 0;
        shell_line(row++, COLOR_CYAN, "00000-0001F, K KEPT . WRITTEN");
        shell_line(row++, COLOR_GRAY, "     0       8       10      18");
        shell_line(row++, COLOR_WHITE, "88H  %s", map88);
        shell_line(row++, COLOR_WHITE, "80H  %s", map80);
        shell_line(row++, COLOR_WHITE, "DMA  %s", dma);
        shell_line(row++, COLOR_CYAN, "WORD AND DWORD WRITES, BYTES 00-0F");

        for (int i = 0; i < STRADDLE_COUNT; i++)
        {
            char word[17];
            char dword[17];

            for (int b = 0; b < 16; b++)
            {
                word[b] = (result->straddle_word[i] >> b) & 1 ? 'W' : '.';
                dword[b] = (result->straddle_dword[i] >> b) & 1 ? 'W' : '.';
            }

            word[16] = dword[16] = 0;
            shell_line(row++, COLOR_WHITE, "%02X %s %s", STRADDLE_FIRST + i, word, dword);
        }

        row++;
        int range = verdict_range(result);
        int straddle = verdict_straddle(result);
        int dma_verdict = verdict_dma(result);
        int bit3 = memcmp(result->map_88, result->map_80, VECTORS) == 0;
        shell_line(row++, COLOR_WHITE, "%s", k_range[range]);
        shell_line(row++, COLOR_WHITE, "%s", k_straddle[straddle]);
        shell_line(row++, COLOR_WHITE, "%s", k_dma[dma_verdict]);
        shell_line(row++, COLOR_WHITE, "BIT 3 %s, 0020H READ %02X", bit3 ? "NO EFFECT" : "CHANGES THE MAP",
            result->read_0020);
        return;
    }

    int undefined = verdict_undefined(result);
    shell_line(row++, COLOR_CYAN, "EXPANSION NMI, 0028H %02X", result->port_0028);
    shell_line(row++, COLOR_WHITE, "BOOT 05C0H %02X 05C2H %02X", result->boot_05c0, result->boot_05c2);
    shell_line(row++, COLOR_WHITE, "BNMI AT BOOT %u", (result->boot_05c0 >> 3) & 1);
    row++;
    shell_line(row++, COLOR_GRAY, "WROTE  05C0H  05C2H");

    for (int i = 0; i < 3; i++)
        shell_line(row++, COLOR_WHITE, " %02X     %02X     %02X", result->written[i], result->read_05c0[i],
            result->read_05c2[i]);

    shell_line(row++, COLOR_WHITE, "RESTORED %02X", result->restored_05c0);
    row++;
    shell_line(row++, COLOR_WHITE, "%s", k_undefined[undefined]);
    shell_line(row++, COLOR_WHITE, "DECODED %s", result->read_05c0[0] == 0xFF && result->read_05c0[1] == 0xFF &&
        result->read_05c0[2] == 0xFF ? "NO" : "YES");
    row++;
    shell_line(row++, COLOR_CYAN, "CDC DMA SECTOR %u: %s %u US", SECTOR_LBA, result->dma.result == CDC_OK ? "OK" :
        result->dma.result == CDC_TIMEOUT ? "TIMEOUT" : "ERROR", result->dma.microseconds);

    for (int i = 0; i < result->dma.status_count && i < 16 && row <= SHELL_BODY_BOTTOM; i += 8)
        shell_line(row++, COLOR_WHITE, "%02X %02X %02X %02X %02X %02X %02X %02X", result->dma.status[i],
            result->dma.status[i + 1], result->dma.status[i + 2], result->dma.status[i + 3], result->dma.status[i + 4],
            result->dma.status[i + 5], result->dma.status[i + 6], result->dma.status[i + 7]);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const protect_t* result = (const protect_t*)data;
    (void)steps;
    fmt_format(line, 34, "RANGE %u STRADDLE %u DMA %u 05C0 %02X", verdict_range(result), verdict_straddle(result),
        verdict_dma(result), result->read_05c0[0]);
    *color = result->dma.result != CDC_OK ? COLOR_YELLOW : COLOR_WHITE;
}
