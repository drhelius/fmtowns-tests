#include "tests.h"

// 1.3 Bus timing (docs/12, HT-4, HT-7, HT-36): one instruction unrolled 16 times, 1,024 iterations, timed with PIT
// counter 0, five repeats, the empty loop subtracted, in every speed mode the machine has and, on the HR, with the
// board cache (00C0h bit 0) both ways. The DRAM patterns look for page-mode effects in main RAM: loads alternating
// between two addresses a stride apart, data in the code's own page, runs with no code fetches and straight code

#define MODES 4
#define LOOPS 80
#define REPEATS 5
#define ITERATIONS 1024
#define STRING_ITERATIONS 32
#define UNROLL 16
#define GROUPS 7

#define TARGET_NONE 0
#define TARGET_RAM 1
#define TARGET_VRAM 2
#define TARGET_VRAM_OFF 3
#define TARGET_FMR 4
#define TARGET_SPRITE 5
#define TARGET_SYSTEM_ROM 6
#define TARGET_FONT_ROM 7
#define TARGET_CMOS 8
#define TARGET_PCM 9
#define TARGET_PORT_FREE 10
#define TARGET_PORT_WAIT 11
#define TARGET_PORT_TIMER 12
#define TARGET_DRAM 13
#define TARGET_OWN_CODE 14

#define FREE_PORT 0x0B70
#define VRAM_OFFSCREEN 0x3E000u
#define FMR_OFFSCREEN 0x000C7D00u
#define SPRITE_PATTERNS 0x10000u
#define DRAM_BASE (SCRATCH_BASE + 0x10000u)

typedef struct
{
    const char* name;
    uint8_t group;
    uint8_t target;
    uint8_t string;
    uint8_t bytes;
    uint16_t table;
    uint16_t dx_cycles;
    uint16_t sx_cycles;
    void (*loop)(void);
    uint32_t stride;
} bus_loop_t;

extern uint32_t bus_time(void (*loop)(void), uint32_t iterations, uint32_t edi, uint32_t edx);
extern uint8_t bus_ram_a[];
extern void bus_empty(void), bus_nop(void), bus_mov_rr(void), bus_add_rr(void), bus_add_imm8(void);
extern void bus_load8(void), bus_store8(void), bus_load16(void), bus_store16(void), bus_load32(void);
extern void bus_store32(void), bus_load32_1(void), bus_load32_2(void), bus_load32_3(void), bus_load16_1(void);
extern void bus_push(void), bus_pop(void), bus_movsd(void), bus_movsw(void), bus_movsb(void), bus_stosd(void);
extern void bus_lodsd(void), bus_mov_imm32(void), bus_add_imm32(void), bus_jmp(void), bus_jmp_add(void);
extern void bus_load_add_imm32(void), bus_in8_30(void), bus_in8(void), bus_out8(void), bus_in16(void);
extern void bus_alt_load16(void), bus_own_code_load16(void), bus_rep_lodsw(void), bus_rep_lodsd(void);
extern void bus_rep_stosw(void), bus_movsd_near(void), bus_load_store16(void), bus_add_m16(void);
extern void bus_alt_store16(void), bus_code_4k(void);

static const char* const k_groups[GROUPS] = { "REFERENCE", "MAIN RAM", "CODE FETCH", "VIDEO MEMORY", "OTHER MEMORY",
    "I/O", "DRAM PATTERNS" };

// 386 table clocks at zero waits in protected mode with CPL <= IOPL, data bus cycles on a DX and an SX
static const bus_loop_t k_loops[] =
{
    { "EMPTY LOOP", 0, TARGET_NONE, 0, 0, 0, 0, 0, bus_empty, 0 },
    { "NOP", 0, TARGET_NONE, 0, 1, 3, 0, 0, bus_nop, 0 },
    { "MOV R,R", 0, TARGET_NONE, 0, 2, 2, 0, 0, bus_mov_rr, 0 },
    { "ADD R,R", 0, TARGET_NONE, 0, 2, 2, 0, 0, bus_add_rr, 0 },
    { "ADD R,IMM8", 0, TARGET_NONE, 0, 3, 2, 0, 0, bus_add_imm8, 0 },
    { "LOAD8", 1, TARGET_RAM, 0, 2, 4, 1, 1, bus_load8, 0 },
    { "STORE8", 1, TARGET_RAM, 0, 2, 2, 1, 1, bus_store8, 0 },
    { "LOAD16", 1, TARGET_RAM, 0, 3, 4, 1, 1, bus_load16, 0 },
    { "STORE16", 1, TARGET_RAM, 0, 3, 2, 1, 1, bus_store16, 0 },
    { "LOAD32", 1, TARGET_RAM, 0, 2, 4, 1, 2, bus_load32, 0 },
    { "STORE32", 1, TARGET_RAM, 0, 2, 2, 1, 2, bus_store32, 0 },
    { "LOAD32 +1", 1, TARGET_RAM, 0, 3, 4, 2, 3, bus_load32_1, 0 },
    { "LOAD32 +2", 1, TARGET_RAM, 0, 3, 4, 2, 2, bus_load32_2, 0 },
    { "LOAD32 +3", 1, TARGET_RAM, 0, 3, 4, 2, 3, bus_load32_3, 0 },
    { "LOAD16 +1", 1, TARGET_RAM, 0, 4, 4, 1, 2, bus_load16_1, 0 },
    { "PUSH R32", 1, TARGET_RAM, 0, 1, 2, 1, 2, bus_push, 0 },
    { "POP R32", 1, TARGET_RAM, 0, 1, 4, 1, 2, bus_pop, 0 },
    { "REP MOVSD 4K", 1, TARGET_RAM, 1, 2, 4103, 2048, 4096, bus_movsd, 0 },
    { "REP MOVSW 4K", 1, TARGET_RAM, 1, 3, 8199, 4096, 4096, bus_movsw, 0 },
    { "REP MOVSB 4K", 1, TARGET_RAM, 1, 2, 16391, 8192, 8192, bus_movsb, 0 },
    { "REP STOSD 4K", 1, TARGET_RAM, 1, 2, 5125, 1024, 2048, bus_stosd, 0 },
    { "LODSD", 1, TARGET_RAM, 0, 1, 5, 1, 2, bus_lodsd, 0 },
    { "MOV R,IMM32", 2, TARGET_NONE, 0, 5, 2, 0, 0, bus_mov_imm32, 0 },
    { "ADD R,IMM32", 2, TARGET_NONE, 0, 6, 2, 0, 0, bus_add_imm32, 0 },
    { "JMP +0", 2, TARGET_NONE, 0, 2, 8, 0, 0, bus_jmp, 0 },
    { "JMP+ADD R,R", 2, TARGET_NONE, 0, 4, 10, 0, 0, bus_jmp_add, 0 },
    { "LOAD+ADD IMM", 2, TARGET_RAM, 0, 8, 6, 1, 2, bus_load_add_imm32, 0 },
    { "VRAM LOAD8", 3, TARGET_VRAM, 0, 2, 4, 1, 1, bus_load8, 0 },
    { "VRAM STORE8", 3, TARGET_VRAM, 0, 2, 2, 1, 1, bus_store8, 0 },
    { "VRAM LOAD16", 3, TARGET_VRAM, 0, 3, 4, 1, 1, bus_load16, 0 },
    { "VRAM STORE16", 3, TARGET_VRAM, 0, 3, 2, 1, 1, bus_store16, 0 },
    { "VRAM LOAD32", 3, TARGET_VRAM, 0, 2, 4, 1, 2, bus_load32, 0 },
    { "VRAM STORE32", 3, TARGET_VRAM, 0, 2, 2, 1, 2, bus_store32, 0 },
    { "VOFF LOAD8", 3, TARGET_VRAM_OFF, 0, 2, 4, 1, 1, bus_load8, 0 },
    { "VOFF STORE8", 3, TARGET_VRAM_OFF, 0, 2, 2, 1, 1, bus_store8, 0 },
    { "VOFF LOAD16", 3, TARGET_VRAM_OFF, 0, 3, 4, 1, 1, bus_load16, 0 },
    { "VOFF STORE16", 3, TARGET_VRAM_OFF, 0, 3, 2, 1, 1, bus_store16, 0 },
    { "VOFF LOAD32", 3, TARGET_VRAM_OFF, 0, 2, 4, 1, 2, bus_load32, 0 },
    { "VOFF STORE32", 3, TARGET_VRAM_OFF, 0, 2, 2, 1, 2, bus_store32, 0 },
    { "FMR LOAD8", 3, TARGET_FMR, 0, 2, 4, 1, 1, bus_load8, 0 },
    { "FMR STORE8", 3, TARGET_FMR, 0, 2, 2, 1, 1, bus_store8, 0 },
    { "FMR LOAD16", 3, TARGET_FMR, 0, 3, 4, 1, 1, bus_load16, 0 },
    { "FMR STORE16", 3, TARGET_FMR, 0, 3, 2, 1, 1, bus_store16, 0 },
    { "SPR LOAD16", 3, TARGET_SPRITE, 0, 3, 4, 1, 1, bus_load16, 0 },
    { "SPR STORE16", 3, TARGET_SPRITE, 0, 3, 2, 1, 1, bus_store16, 0 },
    { "SPR LOAD32", 3, TARGET_SPRITE, 0, 2, 4, 1, 2, bus_load32, 0 },
    { "SPR STORE32", 3, TARGET_SPRITE, 0, 2, 2, 1, 2, bus_store32, 0 },
    { "SYS ROM LOAD16", 4, TARGET_SYSTEM_ROM, 0, 3, 4, 1, 1, bus_load16, 0 },
    { "SYS ROM LOAD32", 4, TARGET_SYSTEM_ROM, 0, 2, 4, 1, 2, bus_load32, 0 },
    { "FONT LOAD32", 4, TARGET_FONT_ROM, 0, 2, 4, 1, 2, bus_load32, 0 },
    { "CMOS LOAD8", 4, TARGET_CMOS, 0, 2, 4, 1, 1, bus_load8, 0 },
    { "PCM LOAD8", 4, TARGET_PCM, 0, 2, 4, 1, 1, bus_load8, 0 },
    { "PCM STORE8", 4, TARGET_PCM, 0, 2, 2, 1, 1, bus_store8, 0 },
    { "IN AL,30H", 5, TARGET_NONE, 0, 2, 6, 1, 1, bus_in8_30, 0 },
    { "IN AL,DX FREE", 5, TARGET_PORT_FREE, 0, 1, 7, 1, 1, bus_in8, 0 },
    { "OUT DX,AL FREE", 5, TARGET_PORT_FREE, 0, 1, 5, 1, 1, bus_out8, 0 },
    { "IN AL,DX 6CH", 5, TARGET_PORT_WAIT, 0, 1, 7, 1, 1, bus_in8, 0 },
    { "OUT DX,AL 6CH", 5, TARGET_PORT_WAIT, 0, 1, 5, 1, 1, bus_out8, 0 },
    { "IN AX,DX 26H", 5, TARGET_PORT_TIMER, 0, 1, 7, 1, 1, bus_in16, 0 },
    { "ALT LD16 4", 6, TARGET_DRAM, 0, 6, 8, 2, 2, bus_alt_load16, 4 },
    { "ALT LD16 64", 6, TARGET_DRAM, 0, 6, 8, 2, 2, bus_alt_load16, 64 },
    { "ALT LD16 256", 6, TARGET_DRAM, 0, 6, 8, 2, 2, bus_alt_load16, 256 },
    { "ALT LD16 512", 6, TARGET_DRAM, 0, 6, 8, 2, 2, bus_alt_load16, 512 },
    { "ALT LD16 1K", 6, TARGET_DRAM, 0, 6, 8, 2, 2, bus_alt_load16, 0x400 },
    { "ALT LD16 2K", 6, TARGET_DRAM, 0, 6, 8, 2, 2, bus_alt_load16, 0x800 },
    { "ALT LD16 4K", 6, TARGET_DRAM, 0, 6, 8, 2, 2, bus_alt_load16, 0x1000 },
    { "ALT LD16 8K", 6, TARGET_DRAM, 0, 6, 8, 2, 2, bus_alt_load16, 0x2000 },
    { "ALT LD16 16K", 6, TARGET_DRAM, 0, 6, 8, 2, 2, bus_alt_load16, 0x4000 },
    { "ALT LD16 64K", 6, TARGET_DRAM, 0, 6, 8, 2, 2, bus_alt_load16, 0x10000 },
    { "LD16 IN CODE", 6, TARGET_OWN_CODE, 0, 3, 4, 1, 1, bus_own_code_load16, 0 },
    { "REP LODSW 4K", 6, TARGET_DRAM, 1, 2, 10245, 2048, 2048, bus_rep_lodsw, 0 },
    { "REP LODSD 4K", 6, TARGET_DRAM, 1, 2, 5125, 1024, 2048, bus_rep_lodsd, 0 },
    { "REP STOSW 4K", 6, TARGET_DRAM, 1, 3, 10245, 2048, 2048, bus_rep_stosw, 0 },
    { "REP MOVSD 512", 6, TARGET_DRAM, 1, 2, 519, 256, 512, bus_movsd_near, 0 },
    { "LD+ST16 SAME", 6, TARGET_DRAM, 0, 6, 6, 2, 2, bus_load_store16, 0 },
    { "ADD M16,R16", 6, TARGET_DRAM, 0, 3, 7, 2, 2, bus_add_m16, 0 },
    { "ALT ST16 4", 6, TARGET_DRAM, 0, 6, 4, 2, 2, bus_alt_store16, 4 },
    { "ALT ST16 64K", 6, TARGET_DRAM, 0, 6, 4, 2, 2, bus_alt_store16, 0x10000 },
    { "CODE 4K MOV", 6, TARGET_NONE, 1, 0, 4096, 1024, 2048, bus_code_4k, 0 }
};

#define LOOP_COUNT ((int)COUNT_OF(k_loops))

typedef struct
{
    uint8_t mode_count;
    uint8_t mhz;
    uint8_t loop_count;
    uint8_t sx;
    uint8_t mode_05e2[MODES];
    uint8_t mode_05e6[MODES];
    uint8_t mode_lspeed[MODES];
    uint8_t mode_cache[MODES];
    uint16_t ticks[MODES][LOOPS][REPEATS];
} bus_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static uint8_t nominal_mhz(void);
static void set_mode(const bus_t* result, int mode);
static void measure(bus_t* result, int mode);
static uint32_t target_address(uint8_t target);
static uint32_t minimum(const bus_t* result, int mode, int loop);
static uint32_t net_ticks(const bus_t* result, int mode, int loop);
static uint32_t clocks_x100(const bus_t* result, int mode, int loop);
static int waits_x10(const bus_t* result, int mode, int loop);
static int find_loop(const char* name);

const test_t g_test_bus =
{
    "1.3", "BUS TIMING", 0, TEST_RUN_ALL, 2, sizeof(bus_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    bus_t* result = (bus_t*)data;
    (void)steps;

    result->mhz = nominal_mhz();
    result->loop_count = (uint8_t)LOOP_COUNT;
    result->sx = g_machine.sx;
    result->mode_05e2[0] = g_boot.port_05e2;
    result->mode_05e6[0] = g_boot.port_05e6;
    result->mode_count = 1;

    if (g_boot.port_05e2 != 0xFF || g_boot.port_05e6 != 0xFF)
    {
        result->mode_05e2[1] = 0x83;
        result->mode_05e6[1] = 0x86;
        result->mode_05e2[2] = 0x00;
        result->mode_05e6[2] = 0x03;
        result->mode_count = 3;
    }

    if (g_machine.sx)
    {
        int mode = result->mode_count++;
        result->mode_05e2[mode] = g_boot.port_05e2;
        result->mode_05e6[mode] = g_boot.port_05e6;
        result->mode_lspeed[mode] = 1;
    }

    for (int mode = 0; mode < result->mode_count; mode++)
        result->mode_cache[mode] = in8(0x00C0);

    if (g_machine.model == 0x07 && result->mode_count < MODES)
    {
        int mode = result->mode_count++;
        result->mode_05e2[mode] = g_boot.port_05e2;
        result->mode_05e6[mode] = g_boot.port_05e6;
        result->mode_cache[mode] = (uint8_t)(in8(0x00C0) ^ 0x01);
    }

    for (int mode = 0; mode < result->mode_count; mode++)
    {
        set_mode(result, mode);
        measure(result, mode);
    }

    set_mode(result, 0);
}

static uint8_t nominal_mhz(void)
{
    switch (g_machine.model)
    {
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
            return 20;
        case 0x0D:
            return 25;
        case 0x0B:
        case 0x0F:
            return 33;
        case 0x0C:
            return 66;
        default:
            return 16;
    }
}

// Wait registers where they read back, LSPEED (0030h bit 0) on the 386SX models
static void set_mode(const bus_t* result, int mode)
{
    if (g_boot.port_05e2 != 0xFF || g_boot.port_05e6 != 0xFF)
    {
        out8(0x05E2, result->mode_05e2[mode]);
        out8(0x05E6, result->mode_05e6[mode]);
    }

    if (result->sx)
        out8(0x0030, result->mode_lspeed[mode]);

    if (g_machine.model == 0x07)
        out8(0x00C0, result->mode_cache[mode]);
}

static void measure(bus_t* result, int mode)
{
    for (int loop = 0; loop < LOOP_COUNT; loop++)
    {
        const bus_loop_t* entry = &k_loops[loop];
        uint32_t iterations = entry->string ? STRING_ITERATIONS : ITERATIONS;
        uint32_t edi = entry->target == TARGET_OWN_CODE ? (uint32_t)entry->loop : target_address(entry->target);
        uint32_t edx = entry->target == TARGET_PORT_WAIT ? 0x006C : entry->target == TARGET_PORT_TIMER ? 0x0026 :
            entry->target == TARGET_DRAM ? edi + entry->stride : FREE_PORT;
        uint8_t saved_0480 = 0;

        if (entry->target == TARGET_DRAM && g_machine.ram_mb < 2)
            continue;

        shell_step("MODE %u %s", mode, entry->name);

        if (entry->target == TARGET_VRAM_OFF)
        {
            out8(0x0440, 0x1C);
            out16(0x0442, 0x0005);
        }
        else if (entry->target == TARGET_CMOS)
        {
            saved_0480 = in8(0x0480);
            out8(0x0480, saved_0480 | 0x01);
        }
        else if (entry->target == TARGET_PCM)
            out8(0x04F7, 0x00);

        for (int repeat = 0; repeat < REPEATS; repeat++)
            result->ticks[mode][loop][repeat] = (uint16_t)bus_time(entry->loop, iterations, edi, edx);

        if (entry->target == TARGET_VRAM_OFF)
        {
            out8(0x0440, 0x1C);
            out16(0x0442, 0x8005);
        }
        else if (entry->target == TARGET_CMOS)
            out8(0x0480, saved_0480);

        tb_sync();
    }
}

static uint32_t target_address(uint8_t target)
{
    switch (target)
    {
        case TARGET_RAM:
            return (uint32_t)bus_ram_a;
        case TARGET_VRAM:
        case TARGET_VRAM_OFF:
            return g_machine.vram + VRAM_OFFSCREEN;
        case TARGET_FMR:
            return FMR_OFFSCREEN;
        case TARGET_SPRITE:
            return g_machine.sprite_ram + SPRITE_PATTERNS;
        case TARGET_SYSTEM_ROM:
            return g_machine.sx ? 0x00FC0000u : 0xFFFC0000u;
        case TARGET_FONT_ROM:
            return g_machine.sx ? 0x00F00000u : 0xC2100000u;
        case TARGET_CMOS:
            return 0x000D8000u;
        case TARGET_PCM:
            return g_machine.pcm_window;
        case TARGET_DRAM:
            return DRAM_BASE;
        default:
            return (uint32_t)bus_ram_a;
    }
}

static uint32_t minimum(const bus_t* result, int mode, int loop)
{
    uint32_t value = 0xFFFF;

    for (int repeat = 0; repeat < REPEATS; repeat++)
    {
        if (result->ticks[mode][loop][repeat] < value)
            value = result->ticks[mode][loop][repeat];
    }

    return value;
}

// The empty loop's share is scaled to the loop's iterations
static uint32_t net_ticks(const bus_t* result, int mode, int loop)
{
    uint32_t base = minimum(result, mode, 0);
    uint32_t ticks = minimum(result, mode, loop);

    if (k_loops[loop].string)
        base = base * STRING_ITERATIONS / ITERATIONS;

    return ticks > base ? ticks - base : 0;
}

// Ticks of 1/307,200 s to clocks per instruction at the nominal clock, x100
static uint32_t clocks_x100(const bus_t* result, int mode, int loop)
{
    uint32_t count = k_loops[loop].string ? STRING_ITERATIONS : ITERATIONS * UNROLL;
    uint32_t microseconds_x100 = net_ticks(result, mode, loop) * 62500u / 192u;

    if (k_loops[loop].string)
        return microseconds_x100 / count * result->mhz;

    return microseconds_x100 * result->mhz / count;
}

// Clocks beyond the table spread over the instruction's bus cycles, code fetches included, x10
static int waits_x10(const bus_t* result, int mode, int loop)
{
    const bus_loop_t* entry = &k_loops[loop];
    int width = result->sx ? 2 : 4;
    int cycles_x10 = (result->sx ? entry->sx_cycles : entry->dx_cycles) * 10 + entry->bytes * 10 / width;
    int extra_x100 = (int)clocks_x100(result, mode, loop) - entry->table * 100;

    if (entry->string)
        cycles_x10 = (result->sx ? entry->sx_cycles : entry->dx_cycles) * 10;

    if (cycles_x10 == 0)
        return 0;

    return extra_x100 / cycles_x10;
}

static int find_loop(const char* name)
{
    for (int loop = 0; loop < LOOP_COUNT; loop++)
    {
        const char* a = k_loops[loop].name;
        const char* b = name;

        while (*a != 0 && *a == *b)
        {
            a++;
            b++;
        }

        if (*a == 0 && *b == 0)
            return loop;
    }

    return 0;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return GROUPS + 1;
}

static void page(const void* data, uint32_t steps, int number)
{
    const bus_t* result = (const bus_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number < GROUPS)
    {
        shell_line(row++, COLOR_CYAN, "%s, MODE 0, %u MHZ", k_groups[number], result->mhz);
        shell_line(row++, COLOR_GRAY, "LOOP          TICKS    CLK  TAB W/B");

        for (int loop = 0; loop < LOOP_COUNT && row <= SHELL_BODY_BOTTOM; loop++)
        {
            if (k_loops[loop].group != number)
                continue;

            uint32_t clocks = clocks_x100(result, 0, loop);
            int waits = waits_x10(result, 0, loop);
            char text[12];

            if (clocks >= 100000)
                fmt_format(text, sizeof(text), "%7u", clocks / 100);
            else
                fmt_format(text, sizeof(text), "%4u.%02u", clocks / 100, clocks % 100);

            shell_line(row++, COLOR_WHITE, "%-14s%5u %s %4u %s%u.%u", k_loops[loop].name, minimum(result, 0, loop),
                text, k_loops[loop].table, waits < 0 ? "-" : " ", (waits < 0 ? -waits : waits) / 10,
                (waits < 0 ? -waits : waits) % 10);
        }

        return;
    }

    static const char* const k_regions[] = { "LOAD16", "VRAM LOAD16", "VOFF LOAD16", "VRAM STORE16", "FMR LOAD8",
        "SPR LOAD16", "SYS ROM LOAD16", "CMOS LOAD8", "PCM LOAD8", "IN AL,DX FREE" };

    shell_line(row++, COLOR_CYAN, "WAITS PER BUS CYCLE BY MODE, X10");

    for (int mode = 0; mode < result->mode_count; mode++)
        shell_line(row++, COLOR_GRAY, "M%u 05E2H %02X 05E6H %02X LSP %u 00C0H %02X", mode, result->mode_05e2[mode],
            result->mode_05e6[mode], result->mode_lspeed[mode], result->mode_cache[mode]);

    shell_line(row++, COLOR_GRAY, "REGION          M0   M1   M2   M3");

    for (unsigned i = 0; i < COUNT_OF(k_regions); i++)
    {
        int loop = find_loop(k_regions[i]);
        char line[48];
        int length = fmt_format(line, sizeof(line), "%-14s", k_regions[i]);

        for (int mode = 0; mode < result->mode_count; mode++)
            length += fmt_format(line + length, sizeof(line) - length, " %4d", waits_x10(result, mode, loop));

        shell_line(row++, COLOR_WHITE, "%s", line);
    }

    int in_wait = find_loop("IN AL,DX 6CH");
    int in_free = find_loop("IN AL,DX FREE");
    int out_wait = find_loop("OUT DX,AL 6CH");
    int out_free = find_loop("OUT DX,AL FREE");
    int in_excess = ((int)clocks_x100(result, 0, in_wait) - (int)clocks_x100(result, 0, in_free)) / result->mhz;
    int out_excess = ((int)clocks_x100(result, 0, out_wait) - (int)clocks_x100(result, 0, out_free)) / result->mhz;

    row++;
    shell_line(row++, COLOR_CYAN, "006CH OVER A FREE PORT, MODE 0");
    shell_line(row++, COLOR_WHITE, "IN  %s%u.%02u US  OUT %s%u.%02u US", in_excess < 0 ? "-" : "",
        (in_excess < 0 ? -in_excess : in_excess) / 100, (in_excess < 0 ? -in_excess : in_excess) % 100,
        out_excess < 0 ? "-" : "", (out_excess < 0 ? -out_excess : out_excess) / 100,
        (out_excess < 0 ? -out_excess : out_excess) % 100);
    shell_line(row++, COLOR_GRAY, "1.00 = THE WAIT ADDS TO THE BUS CYCLE");
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const bus_t* result = (const bus_t*)data;
    (void)steps;
    fmt_format(line, 34, "RAM W %d VRAM W %d 6CH %u", waits_x10(result, 0, find_loop("LOAD16")),
        waits_x10(result, 0, find_loop("VRAM LOAD16")), clocks_x100(result, 0, find_loop("OUT DX,AL 6CH")) / 100);
    *color = COLOR_WHITE;
}
