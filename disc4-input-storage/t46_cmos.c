#include "tests.h"

// 4.6 CMOS contents (docs/12, HT-29), read only: the disc never writes the CMOS. The 2,048 bytes at the even ports
// 3000h-3FFEh and the 8 KiB window (F40000h on the 386SX models, C2140000h on the others), the validity rule of
// the system ROM's CMOS service, the SETUP record at E8h-1E7h, the single-drive byte at 146h, and on the 386SX
// models a map of the backup RAM at F42000h-F47FFFh

#define PORT_BYTES 0x800u
#define WINDOW_BYTES 0x2000u
#define BACKUP_BASE 0x00F42000u
#define BACKUP_BLOCKS 96
#define HEADER_END 0xA4u
#define SETUP_START 0xE8u
#define SETUP_SIZE 0x100u
#define SINGLE_DRIVE 0x146u
#define VALID_TAG 0x4179u
#define MAP_GROUP 32u
#define EXTRA_ROWS 20

typedef struct
{
    uint8_t ran;
    uint8_t window_kept;
    uint8_t backup_read;
    uint8_t setup_sum;
    uint16_t header_sum;
    uint16_t header_stored;
    uint16_t header_tag;
    uint16_t mismatches;
    uint16_t mismatch_first;
    uint8_t single_drive;
    uint8_t reserved;
    uint32_t port_crc;
    uint32_t window_crc;
    uint32_t backup_crc;
    uint8_t backup_map[BACKUP_BLOCKS];
    uint8_t bytes[WINDOW_BYTES];
} cmos_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static int header_valid(const cmos_t* result);
static int setup_valid(const cmos_t* result);
static char group_mark(const uint8_t* bytes, uint32_t size);
static int hex_rows(int row, const cmos_t* result, uint32_t first, uint32_t end);

const test_t g_test_cmos =
{
    "4.6", "CMOS CONTENTS", 0, TEST_RUN_ALL, 1, sizeof(cmos_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    cmos_t* result = (cmos_t*)data;
    uint32_t window = g_machine.sx ? 0x00F40000u : 0xC2140000u;
    uint8_t ports[PORT_BYTES];
    (void)steps;

    shell_step("CMOS PORTS");

    for (uint32_t i = 0; i < PORT_BYTES; i++)
        ports[i] = in8((uint16_t)(0x3000 + i * 2));

    result->port_crc = crc32(ports, PORT_BYTES);
    shell_step("CMOS WINDOW");
    memcpy(result->bytes, (const void*)window, WINDOW_BYTES);
    result->window_crc = crc32(result->bytes, WINDOW_BYTES);
    result->mismatch_first = 0xFFFF;

    for (uint32_t i = 0; i < PORT_BYTES; i++)
    {
        if (result->bytes[i] != ports[i])
        {
            if (result->mismatches == 0)
                result->mismatch_first = (uint16_t)i;

            result->mismatches++;
        }
    }

    result->window_kept = result->mismatches == 0;

    if (!result->window_kept)
        memcpy(result->bytes, ports, PORT_BYTES);

    for (uint32_t i = 0; i < HEADER_END; i += 2)
        result->header_sum = (uint16_t)(result->header_sum + (result->bytes[i] | (result->bytes[i + 1] << 8)));

    result->header_stored = (uint16_t)(result->bytes[0xA4] | (result->bytes[0xA5] << 8));
    result->header_tag = (uint16_t)(result->bytes[0xA6] | (result->bytes[0xA7] << 8));

    for (uint32_t i = 0; i < SETUP_SIZE; i++)
        result->setup_sum = (uint8_t)(result->setup_sum + result->bytes[SETUP_START + i]);

    result->single_drive = result->bytes[SINGLE_DRIVE];

    if (g_machine.sx)
    {
        shell_step("BACKUP RAM");
        result->backup_read = 1;
        result->backup_crc = crc32((const void*)BACKUP_BASE, BACKUP_BLOCKS * 256u);

        for (int block = 0; block < BACKUP_BLOCKS; block++)
            result->backup_map[block] = (uint8_t)group_mark((const uint8_t*)(BACKUP_BASE + (uint32_t)block * 256u), 256);
    }

    result->ran = 1;
}

static int header_valid(const cmos_t* result)
{
    return result->header_sum == result->header_stored && result->header_tag == VALID_TAG;
}

static int setup_valid(const cmos_t* result)
{
    return result->setup_sum == 0;
}

// . all 00h, F all FFh, P the FF 00 pattern, # anything else
static char group_mark(const uint8_t* bytes, uint32_t size)
{
    int zeros = 1;
    int ones = 1;
    int pattern = 1;

    for (uint32_t i = 0; i < size; i++)
    {
        zeros &= bytes[i] == 0x00;
        ones &= bytes[i] == 0xFF;
        pattern &= bytes[i] == ((i & 1) == 0 ? 0xFF : 0x00);
    }

    return zeros ? '.' : ones ? 'F' : pattern ? 'P' : '#';
}

static int hex_rows(int row, const cmos_t* result, uint32_t first, uint32_t end)
{
    for (uint32_t address = first; address < end && row <= SHELL_BODY_BOTTOM; address += 8)
    {
        const uint8_t* bytes = result->bytes + address;
        shell_line(row++, COLOR_WHITE, "%03X %02X %02X %02X %02X %02X %02X %02X %02X", address, bytes[0], bytes[1],
            bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]);
    }

    return row;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 6;
}

static void page(const void* data, uint32_t steps, int number)
{
    const cmos_t* result = (const cmos_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "CMOS, READ ONLY");
        shell_line(row++, COLOR_WHITE, "PORTS CRC %08X", result->port_crc);
        shell_line(row++, COLOR_WHITE, "WINDOW CRC %08X, %s", result->window_crc,
            result->window_kept ? "SAME AS PORTS" : "DIFFERS");

        if (!result->window_kept)
            shell_line(row++, COLOR_YELLOW, "%u BYTES DIFFER, FIRST %03X", result->mismatches, result->mismatch_first);

        row++;
        shell_line(row++, COLOR_GREEN, "HEADER %s", header_valid(result) ? "VALID" : "INVALID");
        shell_line(row++, COLOR_WHITE, " SUM 000-0A3 %04X, A4H %04X", result->header_sum, result->header_stored);
        shell_line(row++, COLOR_WHITE, " A6H %04X (%04X)", result->header_tag, VALID_TAG);
        shell_line(row++, COLOR_GREEN, "SETUP E8-1E7 %s", setup_valid(result) ? "VALID" : "INVALID");
        shell_line(row++, COLOR_WHITE, " BYTE SUM %02X, 1E7H %02X", result->setup_sum, result->bytes[0x1E7]);
        shell_line(row++, COLOR_GREEN, "SINGLE DRIVE 146H: %02X", result->single_drive);
        row++;

        if (result->backup_read)
        {
            char line[40];

            shell_line(row++, COLOR_CYAN, "BACKUP RAM F42000H, 256 B BLOCKS");
            shell_line(row++, COLOR_WHITE, "CRC %08X", result->backup_crc);

            for (int block = 0; block < BACKUP_BLOCKS; block += 32)
            {
                for (int i = 0; i < 32; i++)
                    line[i] = (char)result->backup_map[block + i];

                line[32] = 0;
                shell_line(row++, COLOR_WHITE, "%s", line);
            }

            shell_line(row++, COLOR_GRAY, ". 00  F FF  P FF00  # OTHER");
        }

        return;
    }

    if (number == 1)
    {
        shell_line(row++, COLOR_CYAN, "000H-0A7H, DIRECTORY AND HEADER");
        hex_rows(row, result, 0x000, 0x0A8);
        return;
    }

    if (number == 2 || number == 3)
    {
        uint32_t first = SETUP_START + (uint32_t)(number - 2) * 0x80u;
        shell_line(row++, COLOR_CYAN, "SETUP RECORD %03XH-%03XH", first, first + 0x7F);
        hex_rows(row, result, first, first + 0x80);
        return;
    }

    if (number == 4)
    {
        char line[40];

        shell_line(row++, COLOR_CYAN, "MAP, 32 B GROUPS, 1 KIB A ROW");

        for (uint32_t base = 0; base < WINDOW_BYTES; base += MAP_GROUP * 32)
        {
            for (uint32_t i = 0; i < 32; i++)
                line[i] = group_mark(result->bytes + base + i * MAP_GROUP, MAP_GROUP);

            line[32] = 0;
            shell_line(row++, COLOR_WHITE, "%04X %s", base, line);
        }

        shell_line(row++, COLOR_GRAY, ". 00  F FF  P FF00  # OTHER");
        shell_line(row++, COLOR_GRAY, "PORTS COVER THE FIRST TWO ROWS");
        return;
    }

    shell_line(row++, COLOR_CYAN, "NON-ZERO BYTES PAST 1E7H");

    int shown = 0;

    for (uint32_t address = SETUP_START + SETUP_SIZE; address < WINDOW_BYTES && shown < EXTRA_ROWS; address += 8)
    {
        if (group_mark(result->bytes + address, 8) == '.')
            continue;

        hex_rows(row++, result, address, address + 8);
        shown++;
    }

    if (shown == 0)
        shell_line(row++, COLOR_GRAY, "NONE");
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const cmos_t* result = (const cmos_t*)data;
    (void)steps;

    *color = COLOR_WHITE;
    fmt_format(line, 34, "HEADER %s SETUP %s 146H %02X", header_valid(result) ? "OK" : "BAD",
        setup_valid(result) ? "OK" : "BAD", result->single_drive);
}
