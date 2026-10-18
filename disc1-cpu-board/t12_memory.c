#include "tests.h"
#include "rom_slices.h"

// 1.2 Memory map and ROMs (docs/12): a CRC-32 of each 64 KiB block of the address space, named from a table of every
// 64 KiB slice of the collection's ROM files, the 24-bit wrap, the CMOS windows and the backup RAM

#define BLOCK_SIZE 0x10000u
#define MAX_BLOCKS 256
#define SAMPLE_SIZE 256
#define CMOS_WINDOW_SIZE 0x2000u
#define CMOS_PORT_BYTES 0x800u
#define BACKUP_START 0x00F42000u
#define BACKUP_END 0x00F48000u
#define CMOS_TEST_OFFSET 0x07FEu

#define CODE_RAM 'R'
#define CODE_VRAM 'V'
#define CODE_SPRITE 'S'
#define CODE_PCM 'P'
#define CODE_CMOS 'C'
#define CODE_IO 'I'
#define CODE_BOOT 'B'
#define CODE_FF 'F'
#define CODE_00 '0'
#define CODE_MIRROR 'M'
#define CODE_UNKNOWN '?'

#define STEP_MAP 0x01
#define STEP_CMOS 0x02
#define STEP_BACKUP 0x04
#define STEP_CMOS_WRITE 0x08
#define STEP_BACKUP_WRITE 0x10

typedef struct
{
    uint32_t address;
    uint8_t blocks;
    char code;
} window_t;

typedef struct
{
    uint32_t address;
    uint8_t blocks;
    const char* name;
} rom_window_t;

// The documented windows of the 32-bit models; a zero code means the window is read
static const window_t k_dx_windows[] =
{
    { 0x80000000u, 8, CODE_VRAM }, { 0x80100000u, 8, CODE_VRAM }, { 0x81000000u, 2, CODE_SPRITE },
    { 0xC2000000u, 8, 0 }, { 0xC2080000u, 8, 0 }, { 0xC2100000u, 4, 0 }, { 0xC2140000u, 1, CODE_CMOS },
    { 0xC2180000u, 8, 0 }, { 0xC2200000u, 1, CODE_PCM }, { 0xFFFC0000u, 4, 0 }
};

static const rom_window_t k_sx_roms[] =
{
    { 0x00600000u, 8, "OS" }, { 0x00680000u, 8, "EX0" }, { 0x00700000u, 8, "EX1" }, { 0x00780000u, 8, "EX2" },
    { 0x00800000u, 8, "EX3" }, { 0x00E00000u, 8, "OS UX" }, { 0x00E80000u, 8, "DIC" }, { 0x00F00000u, 4, "FONT" },
    { 0x00FC0000u, 4, "SYS" }
};

static const rom_window_t k_dx_roms[] =
{
    { 0xC2000000u, 8, "OS" }, { 0xC2080000u, 8, "DIC" }, { 0xC2100000u, 4, "FONT" }, { 0xC2180000u, 8, "F20" },
    { 0xFFFC0000u, 4, "SYS" }
};

static const test_step_t k_steps[] =
{
    { "BLOCK MAP AND WRAP", 0 },
    { "CMOS WINDOWS", 0 },
    { "BACKUP RAM CHECKSUM", 0 },
    { "CMOS BYTE WRITE", TEST_CONFIRM },
    { "BACKUP RAM WRITES", TEST_CONFIRM },
    { 0, 0 }
};

typedef struct
{
    uint32_t crc[MAX_BLOCKS];
    uint8_t code[MAX_BLOCKS];
    uint8_t info[MAX_BLOCKS];
    uint8_t slice[MAX_BLOCKS];
    uint8_t first[MAX_BLOCKS][8];
    uint8_t stable[MAX_BLOCKS / 8];
    uint16_t blocks;
    uint8_t dx;
    uint8_t ram_mb;
    uint32_t boot_window_crc;
    uint8_t boot_same_as_system;
    uint8_t wrap_low;
    uint8_t wrap_vram;
    uint8_t port_0480;
    uint32_t cmos_port_crc;
    uint32_t cmos_native_crc;
    uint32_t cmos_window_crc;
    uint32_t cmos_native_even_crc;
    uint8_t cmos_native_byte;
    uint8_t cmos_native_even;
    uint8_t cmos_window_byte;
    uint8_t cmos_window_even;
    uint8_t cmos_write_original;
    uint8_t cmos_write_value;
    uint8_t cmos_write_port_byte;
    uint8_t cmos_write_port_even;
    uint8_t cmos_write_restored;
    uint8_t port_0486;
    uint8_t backup_present;
    uint8_t reserved;
    uint32_t backup_byte_sum;
    uint32_t backup_dword_sum;
    uint32_t backup_head;
    uint32_t backup_tail;
    uint8_t backup_write[3][2][3];
    uint8_t reserved2[2];
} memory_t;

static uint32_t s_tables[4][256];

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void build_tables(void);
static uint32_t block_crc(uint32_t address, uint32_t size);
static int uniform(uint32_t address, uint32_t value);
static int find_slice(uint32_t crc);
static int has_slice(uint32_t crc, int file, int slice);
static int preferred(int file);
static uint32_t block_address(const memory_t* result, int block);
static void map_blocks(memory_t* result);
static void classify(memory_t* result, int block, uint32_t address);
static void check_wrap(memory_t* result);
static void check_cmos(memory_t* result);
static void check_backup(memory_t* result);
static void write_cmos(memory_t* result);
static void write_backup(memory_t* result);
static int rom_window_page(const memory_t* result, int row);
static int map_page(const memory_t* result, int row);
static int count_notable(const memory_t* result);
static void notable_page(const memory_t* result, int first, int row);
static void cmos_page(const memory_t* result, int row);

const test_t g_test_memory =
{
    "1.2", "MEMORY MAP AND ROMS", "CMOS!", TEST_RUN_ALL, 1, sizeof(memory_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    memory_t* result = (memory_t*)data;

    build_tables();
    result->dx = !g_machine.sx;
    result->ram_mb = g_machine.ram_mb;

    if ((steps & STEP_MAP) != 0)
    {
        map_blocks(result);
        check_wrap(result);
    }

    if ((steps & STEP_CMOS) != 0)
        check_cmos(result);

    if ((steps & STEP_BACKUP) != 0)
        check_backup(result);

    if ((steps & STEP_CMOS_WRITE) != 0)
        write_cmos(result);

    if ((steps & STEP_BACKUP_WRITE) != 0)
        write_backup(result);
}

// Slicing by four: four table lookups per 32-bit word
static void build_tables(void)
{
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t value = i;

        for (int bit = 0; bit < 8; bit++)
            value = (value & 1) != 0 ? (value >> 1) ^ 0xEDB88320u : value >> 1;

        s_tables[0][i] = value;
    }

    for (uint32_t i = 0; i < 256; i++)
    {
        for (int t = 1; t < 4; t++)
            s_tables[t][i] = (s_tables[t - 1][i] >> 8) ^ s_tables[0][s_tables[t - 1][i] & 0xFF];
    }
}

static uint32_t block_crc(uint32_t address, uint32_t size)
{
    const volatile uint32_t* words = (const volatile uint32_t*)address;
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < size / 4; i++)
    {
        crc ^= words[i];
        crc = s_tables[3][crc & 0xFF] ^ s_tables[2][(crc >> 8) & 0xFF] ^ s_tables[1][(crc >> 16) & 0xFF] ^
            s_tables[0][crc >> 24];
    }

    return ~crc;
}

static int uniform(uint32_t address, uint32_t value)
{
    const volatile uint32_t* words = (const volatile uint32_t*)address;

    for (uint32_t i = 0; i < BLOCK_SIZE / 4; i++)
    {
        if (words[i] != value)
            return 0;
    }

    return 1;
}

// The first entry with the CRC, or -1
static int find_slice(uint32_t crc)
{
    int low = 0;
    int high = ROM_SLICE_COUNT;

    while (low < high)
    {
        int middle = (low + high) / 2;

        if (g_rom_slices[middle].crc < crc)
            low = middle + 1;
        else
            high = middle;
    }

    return low < ROM_SLICE_COUNT && g_rom_slices[low].crc == crc ? low : -1;
}

static int has_slice(uint32_t crc, int file, int slice)
{
    int index = find_slice(crc);

    for (; index >= 0 && index < ROM_SLICE_COUNT && g_rom_slices[index].crc == crc; index++)
    {
        if (g_rom_slices[index].file == file && g_rom_slices[index].slice == slice)
            return 1;
    }

    return 0;
}

// The machine's own set comes first when several files hold the same data
static int preferred(int file)
{
    static const struct { uint8_t model; const char* prefix; } k_sets[] =
    {
        { 0x01, "M12" }, { 0x02, "UX" }, { 0x03, "UX" }, { 0x04, "UX" }, { 0x05, "UX" }, { 0x06, "UX" },
        { 0x07, "HR" }, { 0x08, "HR" }, { 0x0C, "MX" }, { 0x0F, "V10" }, { 0x4A, "MAR" }
    };
    const char* name = g_rom_files[file].name;

    for (unsigned i = 0; i < COUNT_OF(k_sets); i++)
    {
        if (k_sets[i].model != g_machine.model)
            continue;

        const char* prefix = k_sets[i].prefix;
        int j = 0;

        while (prefix[j] != 0 && prefix[j] == name[j])
            j++;

        return prefix[j] == 0;
    }

    return 0;
}

static uint32_t block_address(const memory_t* result, int block)
{
    if (!result->dx)
        return (uint32_t)block << 16;

    for (unsigned i = 0; i < COUNT_OF(k_dx_windows); i++)
    {
        if (block < k_dx_windows[i].blocks)
            return k_dx_windows[i].address + (uint32_t)block * BLOCK_SIZE;

        block -= k_dx_windows[i].blocks;
    }

    return 0;
}

static void map_blocks(memory_t* result)
{
    uint32_t ram_blocks = (uint32_t)result->ram_mb * 16;

    result->blocks = 0;

    if (result->dx)
    {
        for (unsigned i = 0; i < COUNT_OF(k_dx_windows); i++)
        {
            for (int b = 0; b < k_dx_windows[i].blocks; b++)
            {
                int block = result->blocks++;
                uint32_t address = k_dx_windows[i].address + (uint32_t)b * BLOCK_SIZE;
                shell_step("BLOCK %08X", address);

                if (k_dx_windows[i].code != 0)
                    result->code[block] = (uint8_t)k_dx_windows[i].code;
                else
                    classify(result, block, address);

                tb_now();
            }
        }

        return;
    }

    for (int block = 0; block < MAX_BLOCKS; block++)
    {
        uint32_t address = (uint32_t)block << 16;
        result->blocks++;

        if ((block & 0x0F) == 0)
            shell_step("BLOCK %06X", address);

        if (block == 0x0C)
            result->code[block] = CODE_IO;
        else if (block == 0x0F)
        {
            result->code[block] = CODE_BOOT;
            result->boot_window_crc = block_crc(0x000F8000u, 0x8000);
            result->boot_same_as_system = block_crc(0x00FF8000u, 0x8000) == result->boot_window_crc;
        }
        else if (block < 0x10 && block != 0x0D)
            result->code[block] = CODE_RAM;
        else if (block >= 0x10 && (uint32_t)block < ram_blocks)
            result->code[block] = CODE_RAM;
        else if ((block >= 0xA0 && block <= 0xA7) || (block >= 0xB0 && block <= 0xB7))
            result->code[block] = CODE_VRAM;
        else if (block == 0xC0 || block == 0xC1)
            result->code[block] = CODE_SPRITE;
        else if (block == 0xF4)
            result->code[block] = CODE_CMOS;
        else if (block == 0xF8)
            result->code[block] = CODE_PCM;
        else
            classify(result, block, address);

        tb_now();
    }
}

// Fills first, then the ROM table, then a mirror of a lower block, and what is left is read twice to see whether
// it holds still
static void classify(memory_t* result, int block, uint32_t address)
{
    const volatile uint8_t* bytes = (const volatile uint8_t*)address;

    for (int i = 0; i < 8; i++)
        result->first[block][i] = bytes[i];

    if (uniform(address, 0xFFFFFFFFu))
    {
        result->code[block] = CODE_FF;
        result->crc[block] = block_crc(address, BLOCK_SIZE);
        result->stable[block >> 3] |= (uint8_t)(1 << (block & 7));
        return;
    }

    if (uniform(address, 0))
    {
        result->code[block] = CODE_00;
        result->crc[block] = block_crc(address, BLOCK_SIZE);
        result->stable[block >> 3] |= (uint8_t)(1 << (block & 7));
        return;
    }

    uint32_t crc = block_crc(address, BLOCK_SIZE);
    int slice = find_slice(crc);
    result->crc[block] = crc;

    if (slice >= 0)
    {
        for (int i = slice; i < ROM_SLICE_COUNT && g_rom_slices[i].crc == crc; i++)
        {
            if (preferred(g_rom_slices[i].file))
            {
                slice = i;
                break;
            }
        }

        result->code[block] = (uint8_t)g_rom_files[g_rom_slices[slice].file].kind;
        result->info[block] = g_rom_slices[slice].file;
        result->slice[block] = g_rom_slices[slice].slice;
        result->stable[block >> 3] |= (uint8_t)(1 << (block & 7));
        return;
    }

    if (!result->dx)
    {
        static const int k_distances[] = { 8, 16, 32, 64, 128 };
        uint32_t ram_size = (uint32_t)result->ram_mb << 20;

        for (unsigned i = 0; i < COUNT_OF(k_distances); i++)
        {
            int lower = block - k_distances[i];

            if (lower >= 0 && memcmp((const void*)address, (const void*)((uint32_t)lower << 16), BLOCK_SIZE) == 0)
            {
                result->code[block] = CODE_MIRROR;
                result->info[block] = (uint8_t)lower;
                return;
            }
        }

        if (ram_size != 0 && memcmp((const void*)address, (const void*)(address & (ram_size - 1)), BLOCK_SIZE) == 0)
        {
            result->code[block] = CODE_MIRROR;
            result->info[block] = (uint8_t)((address & (ram_size - 1)) >> 16);
            return;
        }
    }

    result->code[block] = CODE_UNKNOWN;

    if (block_crc(address, BLOCK_SIZE) == crc)
        result->stable[block >> 3] |= (uint8_t)(1 << (block & 7));
}

static void check_wrap(memory_t* result)
{
    if (result->dx)
        return;

    shell_step("24-BIT WRAP");
    result->wrap_low = memcmp((const void*)0x01000000u, (const void*)0x00000000u, SAMPLE_SIZE) == 0;
    result->wrap_vram = memcmp((const void*)0x01A00000u, (const void*)0x00A00000u, SAMPLE_SIZE) == 0;
}

// The native window (F40000h on the SX models) and D8000h with 0480h bit 0 set, against the 2,048 bytes of the
// ports at 3000h, both byte for byte and with the window's even bytes only
static void check_cmos(memory_t* result)
{
    uint32_t native = result->dx ? 0xC2140000u : 0x00F40000u;
    uint8_t ports[CMOS_PORT_BYTES];

    shell_step("CMOS WINDOWS");

    for (uint32_t i = 0; i < CMOS_PORT_BYTES; i++)
        ports[i] = in8((uint16_t)(0x3000 + i * 2));

    result->port_0480 = in8(0x0480);
    result->cmos_port_crc = crc32(ports, CMOS_PORT_BYTES);
    result->cmos_native_crc = block_crc(native, CMOS_WINDOW_SIZE);
    result->cmos_native_byte = memcmp((const void*)native, ports, CMOS_PORT_BYTES) == 0;
    result->cmos_native_even = 1;

    for (uint32_t i = 0; i < CMOS_PORT_BYTES; i++)
    {
        if (MMIO8(native + i * 2) != ports[i])
            result->cmos_native_even = 0;
    }

    out8(0x0480, result->port_0480 | 0x01);
    result->cmos_window_crc = block_crc(0x000D8000u, CMOS_WINDOW_SIZE);
    result->cmos_window_byte = memcmp((const void*)0x000D8000u, ports, CMOS_PORT_BYTES) == 0;
    result->cmos_window_even = 1;

    for (uint32_t i = 0; i < CMOS_PORT_BYTES; i++)
    {
        if (MMIO8(0x000D8000u + i * 2) != ports[i])
            result->cmos_window_even = 0;
    }

    out8(0x0480, result->port_0480);
}

// The V09 reset path's check: F42004h-F47FFBh added up against the dwords at F42000h and F47FFCh
static void check_backup(memory_t* result)
{
    if (result->dx)
        return;

    shell_step("BACKUP RAM");
    result->backup_present = 1;
    result->port_0486 = in8(0x0486);
    result->backup_head = MMIO32(BACKUP_START);
    result->backup_tail = MMIO32(BACKUP_END - 4);

    for (uint32_t address = BACKUP_START + 4; address < BACKUP_END - 4; address += 4)
    {
        uint32_t value = MMIO32(address);
        result->backup_dword_sum += value;
        result->backup_byte_sum += (value & 0xFF) + ((value >> 8) & 0xFF) + ((value >> 16) & 0xFF) + (value >> 24);
    }
}

static void write_cmos(memory_t* result)
{
    uint32_t native = result->dx ? 0xC2140000u : 0x00F40000u;
    volatile uint8_t* byte = (volatile uint8_t*)(native + CMOS_TEST_OFFSET);

    shell_step("CMOS BYTE %04X", CMOS_TEST_OFFSET);
    result->cmos_write_original = *byte;
    result->cmos_write_value = result->cmos_write_original ^ 0x5A;
    *byte = result->cmos_write_value;
    result->cmos_write_port_byte = in8((uint16_t)(0x3000 + CMOS_TEST_OFFSET * 2));
    result->cmos_write_port_even = in8((uint16_t)(0x3000 + CMOS_TEST_OFFSET));
    *byte = result->cmos_write_original;
    result->cmos_write_restored = *byte;
}

// Each 8 KiB block with its 0486h bit set and clear: the byte written, the byte read back, the byte put back
static void write_backup(memory_t* result)
{
    if (result->dx)
        return;

    uint8_t saved = in8(0x0486);
    result->port_0486 = saved;

    for (int block = 0; block < 3; block++)
    {
        volatile uint8_t* byte = (volatile uint8_t*)(BACKUP_START + (uint32_t)block * 0x2000 + 0x100);

        for (int state = 0; state < 2; state++)
        {
            uint8_t bit = (uint8_t)(0x02 << block);
            shell_step("BACKUP BLOCK %u BIT %u", block, state);
            out8(0x0486, state != 0 ? (uint8_t)(saved | bit) : (uint8_t)(saved & ~bit));
            uint8_t original = *byte;
            result->backup_write[block][state][0] = original ^ 0xA5;
            *byte = original ^ 0xA5;
            result->backup_write[block][state][1] = *byte;
            *byte = original;
            result->backup_write[block][state][2] = *byte == original;
        }
    }

    out8(0x0486, saved);
}

static int pages(const void* data, uint32_t steps)
{
    const memory_t* result = (const memory_t*)data;
    int count = 0;

    if ((steps & STEP_MAP) != 0)
        count += 2 + (count_notable(result) + 15) / 16;

    if ((steps & ~STEP_MAP) != 0)
        count++;

    return count;
}

static void page(const void* data, uint32_t steps, int number)
{
    const memory_t* result = (const memory_t*)data;
    int row = SHELL_BODY_TOP;

    if ((steps & STEP_MAP) != 0)
    {
        int notable_pages = (count_notable(result) + 15) / 16;

        if (number == 0)
        {
            rom_window_page(result, row);
            return;
        }

        if (number == 1)
        {
            map_page(result, row);
            return;
        }

        if (number < 2 + notable_pages)
        {
            notable_page(result, (number - 2) * 16, row);
            return;
        }
    }

    cmos_page(result, row);
}

static int rom_window_page(const memory_t* result, int row)
{
    const rom_window_t* windows = result->dx ? k_dx_roms : k_sx_roms;
    int count = result->dx ? (int)COUNT_OF(k_dx_roms) : (int)COUNT_OF(k_sx_roms);

    shell_line(row++, COLOR_CYAN, "ROM WINDOWS, 64 KIB SLICES");

    for (int w = 0; w < count; w++)
    {
        const rom_window_t* window = &windows[w];
        int blocks[32];
        int found = 0;
        int whole = -1;

        for (int b = 0; b < result->blocks && found < window->blocks; b++)
        {
            uint32_t address = block_address(result, b);

            if (address >= window->address && address < window->address + window->blocks * BLOCK_SIZE)
                blocks[found++] = b;
        }

        if (found != window->blocks)
            continue;

        for (int pass = 0; pass < 2 && whole < 0; pass++)
        {
            for (int file = 0; file < ROM_FILE_COUNT && whole < 0; file++)
            {
                int match = g_rom_files[file].slices == window->blocks && preferred(file) == (pass == 0);

                for (int k = 0; k < found && match; k++)
                    match = has_slice(result->crc[blocks[k]], file, k);

                if (match)
                    whole = file;
            }
        }

        if (whole >= 0)
        {
            shell_line(row++, COLOR_GREEN, "%-5s %08X %s %08X", window->name, window->address, g_rom_files[whole].name,
                g_rom_files[whole].crc);
            continue;
        }

        int named = 0;
        int file = -1;

        for (int k = 0; k < found; k++)
        {
            uint8_t code = result->code[blocks[k]];

            if (code == 'Y' || code == 'O' || code == 'E' || code == 'D' || code == 'N' || code == 'T')
            {
                named++;

                if (file < 0)
                    file = result->info[blocks[k]];
            }
        }

        uint8_t code = result->code[blocks[0]];

        if (named == 0)
            shell_line(row++, COLOR_WHITE, "%-5s %08X %s", window->name, window->address, code == CODE_FF ? "FF FILL" :
                code == CODE_00 ? "00 FILL" : code == CODE_MIRROR ? "MIRROR" : "UNKNOWN");
        else
            shell_line(row++, COLOR_YELLOW, "%-5s %08X %s %u/%u MIXED", window->name, window->address,
                g_rom_files[file].name, named, window->blocks);
    }

    if (!result->dx)
    {
        row++;
        shell_line(row++, COLOR_WHITE, "BOOT F8000-FFFFF %08X", result->boot_window_crc);
        shell_line(row++, result->boot_same_as_system ? COLOR_GREEN : COLOR_YELLOW, "%s",
            result->boot_same_as_system ? "SAME AS THE SYSTEM ROM'S END" : "DIFFERS FROM THE SYSTEM ROM'S END");
        row++;
        shell_line(row++, COLOR_WHITE, "WRAP 1000000=000000 %s", result->wrap_low ? "YES" : "NO");
        shell_line(row++, COLOR_WHITE, "WRAP 1A00000=A00000 %s", result->wrap_vram ? "YES" : "NO");
    }

    return row;
}

static int map_page(const memory_t* result, int row)
{
    if (result->dx)
    {
        int block = 0;

        for (unsigned i = 0; i < COUNT_OF(k_dx_windows); i++)
        {
            char letters[33];
            int b = 0;

            for (; b < k_dx_windows[i].blocks; b++)
                letters[b] = (char)result->code[block + b];

            letters[b] = 0;
            shell_line(row++, COLOR_WHITE, "%08X %s", k_dx_windows[i].address, letters);
            block += k_dx_windows[i].blocks;
        }
    }
    else
    {
        shell_line(row++, COLOR_GRAY, "   0123456789ABCDEF X 64 KIB");

        for (int line = 0; line < 16; line++)
        {
            char letters[17];

            for (int b = 0; b < 16; b++)
                letters[b] = (char)result->code[line * 16 + b];

            letters[16] = 0;
            shell_line(row++, COLOR_WHITE, "%X0 %s", line, letters);
        }
    }

    row++;
    shell_line(row++, COLOR_GRAY, "R RAM V VRAM S SPRITE P PCM C CMOS");
    shell_line(row++, COLOR_GRAY, "I FM-R IO B BOOT Y SYS O OS E EXT");
    shell_line(row++, COLOR_GRAY, "D DIC N FONT T F20 F FF 0 00");
    shell_line(row++, COLOR_GRAY, "M MIRROR ? UNKNOWN");
    return row;
}

static int count_notable(const memory_t* result)
{
    int count = 0;

    for (int b = 0; b < result->blocks; b++)
    {
        if (result->code[b] == CODE_UNKNOWN || result->code[b] == CODE_MIRROR)
            count++;
    }

    return count;
}

static void notable_page(const memory_t* result, int first, int row)
{
    int index = 0;

    shell_line(row++, COLOR_CYAN, "MIRRORS AND UNKNOWN BLOCKS");

    for (int b = 0; b < result->blocks && row <= SHELL_BODY_BOTTOM; b++)
    {
        uint8_t code = result->code[b];

        if (code != CODE_UNKNOWN && code != CODE_MIRROR)
            continue;

        if (index++ < first)
            continue;

        uint32_t address = block_address(result, b);

        if (code == CODE_MIRROR)
        {
            shell_line(row++, COLOR_WHITE, "%08X MIRROR OF %08X", address, (uint32_t)result->info[b] << 16);
            continue;
        }

        const uint8_t* bytes = result->first[b];
        int stable = (result->stable[b >> 3] >> (b & 7)) & 1;
        shell_line(row++, stable ? COLOR_WHITE : COLOR_YELLOW, "%08X %02X%02X%02X%02X%02X%02X%02X%02X %s", address,
            bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], stable ? "SAME" : "FLOAT");
    }
}

static void cmos_page(const memory_t* result, int row)
{
    shell_line(row++, COLOR_CYAN, "CMOS");
    shell_line(row++, COLOR_WHITE, "PORTS 3000H-3FFEH CRC %08X", result->cmos_port_crc);
    shell_line(row++, COLOR_WHITE, "%s %08X BYTES %s EVEN %s", result->dx ? "C2140000" : "F40000",
        result->cmos_native_crc, result->cmos_native_byte ? "=" : "X", result->cmos_native_even ? "=" : "X");
    shell_line(row++, COLOR_WHITE, "D8000 %08X BYTES %s EVEN %s", result->cmos_window_crc,
        result->cmos_window_byte ? "=" : "X", result->cmos_window_even ? "=" : "X");
    shell_line(row++, COLOR_GRAY, "= SAME AS THE PORTS  0480H %02X", result->port_0480);

    if (result->cmos_write_value != result->cmos_write_original)
    {
        row++;
        shell_line(row++, COLOR_WHITE, "WINDOW %04X %02X>%02X PORT %02X/%02X BACK %s", CMOS_TEST_OFFSET,
            result->cmos_write_original, result->cmos_write_value, result->cmos_write_port_byte,
            result->cmos_write_port_even, result->cmos_write_restored == result->cmos_write_original ? "OK" : "NO");
    }

    if (!result->backup_present)
        return;

    row++;
    shell_line(row++, COLOR_CYAN, "BACKUP RAM F42000H-F47FFFH, 0486H %02X", result->port_0486);
    shell_line(row++, COLOR_WHITE, "HEAD %08X TAIL %08X", result->backup_head, result->backup_tail);
    shell_line(row++, COLOR_WHITE, "SUM BYTES %08X DWORDS %08X", result->backup_byte_sum, result->backup_dword_sum);
    shell_line(row++, result->backup_byte_sum == result->backup_head || result->backup_dword_sum == result->backup_head ?
        COLOR_GREEN : COLOR_YELLOW, "%s", result->backup_byte_sum == result->backup_head ? "BYTE SUM MATCHES THE HEAD" :
        result->backup_dword_sum == result->backup_head ? "DWORD SUM MATCHES THE HEAD" : "NO SUM MATCHES THE HEAD");

    if (result->backup_write[0][0][0] != 0 || result->backup_write[0][1][0] != 0)
    {
        row++;
        shell_line(row++, COLOR_WHITE, "BLOCK  BIT CLEAR      BIT SET");

        for (int block = 0; block < 3; block++)
        {
            const uint8_t (*writes)[3] = result->backup_write[block];
            shell_line(row++, COLOR_WHITE, "%06X W%02X R%02X %s  W%02X R%02X %s", BACKUP_START + block * 0x2000,
                writes[0][0], writes[0][1], writes[0][2] ? "OK" : "NO", writes[1][0], writes[1][1],
                writes[1][2] ? "OK" : "NO");
        }
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const memory_t* result = (const memory_t*)data;
    int roms = 0;
    int unknown = 0;
    (void)steps;

    for (int b = 0; b < result->blocks; b++)
    {
        uint8_t code = result->code[b];

        if (code == 'Y' || code == 'O' || code == 'E' || code == 'D' || code == 'N' || code == 'T')
            roms++;
        else if (code == CODE_UNKNOWN)
            unknown++;
    }

    fmt_format(line, 34, "ROM %u UNKNOWN %u SYS %08X", roms, unknown, result->crc[result->dx ? 48 : 0xFC]);
    *color = unknown != 0 ? COLOR_YELLOW : COLOR_WHITE;
}
