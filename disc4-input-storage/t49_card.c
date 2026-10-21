#include "tests.h"

// 4.9 IC memory card (Data Book §3.6.5 and ch. 14), read only: the card is never written. 048Ah status (CHANGE bit 7,
// battery bits 5-4, RDY bit 3, CD bits 2-1, WP bit 0), the JEIDA 4 bank register 0490h and 0491h, and the window
// (D00000h on the 386SX models, C0000000h on the others): the first bytes of banks 0-3 and of the attribute space,
// with a CRC-32 of each bank's first 4 KiB. A watch logs 048Ah while someone takes the card out and puts it back

#define BANKS 4
#define KEEP 16
#define ATTRIBUTE 32
#define CHANGES 32
#define WATCH_US 30000000u
#define CRC_BYTES 4096u

typedef struct
{
    uint32_t time;
    uint8_t value;
    uint8_t reserved[3];
} card_change_t;

typedef struct
{
    uint8_t ran;
    uint8_t status[2];
    uint8_t bank_found;
    uint8_t space_found;
    uint8_t bank_reads[2];
    uint8_t change_count;
    uint32_t window;
    uint32_t bank_crc[BANKS];
    uint8_t bank_bytes[BANKS][KEEP];
    uint8_t attribute[ATTRIBUTE];
    uint8_t first_watch;
    uint8_t reserved[3];
    card_change_t changes[CHANGES];
} card_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void step_window(card_t* result);
static void step_watch(card_t* result);
static const char* presence(uint8_t status);

static const test_step_t k_steps[] =
{
    { "REGISTERS AND WINDOW", 0 },
    { "CARD OUT AND IN", TEST_PHOTO },
    { 0, 0 }
};

const test_t g_test_card =
{
    "4.9", "IC MEMORY CARD", "CARD", TEST_RUN_ALL, 1, sizeof(card_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    card_t* result = (card_t*)data;

    ui_pad_only(0);

    if ((steps & 0x01) != 0)
        step_window(result);

    if ((steps & 0x02) != 0)
        step_watch(result);
}

// Each bank selected in turn, then attribute space on bank 0; both registers put back as found
static void step_window(card_t* result)
{
    shell_step("CARD REGISTERS AND WINDOW");
    result->window = g_machine.sx ? 0x00D00000u : 0xC0000000u;
    result->status[0] = in8(0x048A);
    result->status[1] = in8(0x048A);
    result->bank_found = in8(0x0490);
    result->space_found = in8(0x0491);

    for (int bank = 0; bank < BANKS; bank++)
    {
        out8(0x0490, (uint8_t)bank);

        if (bank == 1)
            result->bank_reads[0] = in8(0x0490);

        for (int i = 0; i < KEEP; i++)
            result->bank_bytes[bank][i] = MMIO8(result->window + (uint32_t)i);

        result->bank_crc[bank] = crc32((const void*)result->window, CRC_BYTES);
    }

    out8(0x0490, 0x00);
    out8(0x0491, 0x01);
    result->bank_reads[1] = in8(0x0491);

    for (int i = 0; i < ATTRIBUTE; i++)
        result->attribute[i] = MMIO8(result->window + (uint32_t)i);

    out8(0x0491, result->space_found & 0x01);
    out8(0x0490, result->bank_found & 0x3F);
    result->ran |= 0x01;
}

static void step_watch(card_t* result)
{
    uint32_t start;
    uint32_t drawn;
    uint8_t last;

    shell_step("CARD OUT AND IN");
    ui_prompt("TAKE THE CARD OUT, WAIT 2 S, PUT IT", "BACK; TWICE. A WHEN DONE");
    result->first_watch = in8(0x048A);
    last = result->first_watch;
    start = tb_now();
    drawn = start;
    (void)ui_poll();

    while (tb_now() - start < WATCH_US)
    {
        uint8_t value = in8(0x048A);

        if (value != last && result->change_count < CHANGES)
        {
            result->changes[result->change_count].time = tb_now() - start;
            result->changes[result->change_count].value = value;
            result->change_count++;
        }

        last = value;

        if (tb_now() - drawn >= 100000u)
        {
            if (ui_poll() == UI_A)
                break;

            ui_note(0, COLOR_WHITE, "048AH %02X, %u CHANGES", last, result->change_count);
            ui_countdown(start, WATCH_US, "A WHEN DONE");
            drawn = tb_now();
        }
    }

    result->ran |= 0x02;
}

static const char* presence(uint8_t status)
{
    switch ((status >> 1) & 0x03)
    {
        case 0:
            return "PRESENT";
        case 3:
            return "ABSENT";
        default:
            return "HALF IN";
    }
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 2;
}

static void page(const void* data, uint32_t steps, int number)
{
    const card_t* result = (const card_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "IC MEMORY CARD, READ ONLY");

        if ((result->ran & 0x01) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        shell_line(row++, COLOR_GREEN, "048AH %02X %02X: %s, WP %u", result->status[0], result->status[1],
            presence(result->status[1]), result->status[1] & 1);
        shell_line(row++, COLOR_WHITE, " CHANGE %u BATTERY %u%u RDY %u", result->status[0] >> 7,
            (result->status[1] >> 5) & 1, (result->status[1] >> 4) & 1, (result->status[1] >> 3) & 1);
        shell_line(row++, COLOR_WHITE, "0490H %02X, BANK 1 READS %02X", result->bank_found, result->bank_reads[0]);
        shell_line(row++, COLOR_WHITE, "0491H %02X, ATTRIBUTE READS %02X", result->space_found, result->bank_reads[1]);
        shell_line(row++, COLOR_CYAN, "WINDOW %08X, FIRST BYTES", result->window);

        for (int bank = 0; bank < BANKS; bank++)
        {
            const uint8_t* bytes = result->bank_bytes[bank];
            shell_line(row++, COLOR_WHITE, "BANK %u CRC %08X", bank, result->bank_crc[bank]);
            shell_line(row++, COLOR_WHITE, " %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
                bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9],
                bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
        }

        shell_line(row++, COLOR_CYAN, "ATTRIBUTE SPACE");

        for (int i = 0; i < ATTRIBUTE; i += 16)
        {
            const uint8_t* bytes = result->attribute + i;
            shell_line(row++, COLOR_WHITE, " %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
                bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9],
                bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
        }

        return;
    }

    shell_line(row++, COLOR_CYAN, "CARD OUT AND IN: 048AH, MS");

    if ((result->ran & 0x02) == 0)
    {
        shell_line(row++, COLOR_GRAY, "NOT RUN");
        return;
    }

    shell_line(row++, COLOR_WHITE, "START  %02X %s", result->first_watch, presence(result->first_watch));

    for (int i = 0; i < result->change_count && row <= SHELL_BODY_BOTTOM - 1; i++)
        shell_line(row++, COLOR_WHITE, "%6u %02X %s%s", result->changes[i].time / 1000, result->changes[i].value,
            presence(result->changes[i].value), (result->changes[i].value & 0x80) != 0 ? " CHANGE" : "");

    shell_line(row++, COLOR_GRAY, "BIT 7 CHANGE, 2-1 CD, 0 WP");
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const card_t* result = (const card_t*)data;
    (void)steps;

    *color = COLOR_WHITE;

    if ((result->ran & 0x01) == 0)
        fmt_copy(line, "NOT RUN", 34);
    else
        fmt_format(line, 34, "048AH %02X, CARD %s", result->status[1], presence(result->status[1]));
}
