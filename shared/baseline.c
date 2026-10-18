#include "baseline.h"
#include "layout.h"
#include "machine.h"
#include "pic.h"
#include "record.h"
#include "screen.h"
#include "timebase.h"
#include "towns.h"

#define SPRITE_RAM_SIZE 0x20000
#define PCM_BANK_SIZE 0x1000
#define PCM_BANKS 16
#define CMOS_BYTES 0x800
#define FMR_REGISTERS 0x000CFF80u
#define WRPROT_VALUE 0x88

// Ports that read back without side effects and don't change while nothing runs, with the bits that count
static const baseline_register_t k_registers[] =
{
    { 0x0002, 0xFF }, { 0x0012, 0xFF }, { 0x0060, 0xFC }, { 0x006A, 0xFF }, { 0x006B, 0xFF }, { 0x0028, 0xFF },
    { 0x0030, 0xFF }, { 0x00AF, 0xFF }, { 0x0404, 0xFF }, { 0x0480, 0xFF }, { 0x04C0, 0xFF }, { 0x04D8, 0x03 },
    { 0x04E9, 0xFF }, { 0x04EA, 0xFF }, { 0x05E2, 0xFF }, { 0x05E6, 0xFF }, { 0x05EC, 0xFF }, { 0x0604, 0xFF },
    { 0x0458, 0xFF }, { 0x045A, 0xFF }, { 0x045B, 0xFF }, { 0x05C0, 0xFF }, { 0x0484, 0xFF }
};

static void reset_pics(void);
static void reset_timers(void);
static void reset_sound(void);
static void reset_cdc(void);
static void reset_floppy(void);
static void reset_keyboard(void);
static void reset_video(int screen_mode);
static void reset_scratch(void);
static void ym_wait(void);

void baseline_apply(int screen_mode)
{
    cli();
    reset_pics();
    reset_timers();
    out8(0x00AF, 0x0F);
    reset_sound();
    reset_cdc();
    reset_floppy();
    reset_keyboard();
    out8(0x04D6, 0x0F);
    out8(0x0404, g_boot.port_0404);
    out8(0x0480, g_boot.port_0480);
    out8(0x0020, WRPROT_VALUE);
    reset_video(screen_mode);
    reset_scratch();
    tb_sync();
}

void baseline_take(baseline_snapshot_t* snapshot)
{
    uint16_t irr = pic_read_irr();
    uint16_t isr = pic_read_isr();

    for (int i = 0; i < BASELINE_VALUES; i++)
        snapshot->values[i] = 0;

    for (unsigned i = 0; i < COUNT_OF(k_registers); i++)
        snapshot->values[i] = in8(k_registers[i].port) & k_registers[i].mask;

    snapshot->values[BASELINE_VALUES - 4] = (uint8_t)irr;
    snapshot->values[BASELINE_VALUES - 3] = (uint8_t)(irr >> 8);
    snapshot->values[BASELINE_VALUES - 2] = (uint8_t)isr;
    snapshot->values[BASELINE_VALUES - 1] = (uint8_t)(isr >> 8);

    uint32_t crc = 0xFFFFFFFFu;

    for (uint16_t i = 0; i < CMOS_BYTES; i++)
    {
        uint8_t value = in8((uint16_t)(0x3000 + i * 2));
        crc = crc32_update(crc, &value, 1);
    }

    snapshot->cmos_crc = ~crc;
    snapshot->backup_crc = g_machine.sx ? crc32((const void*)0x00F42000u, 0x6000) : 0;
    snapshot->crc = crc32(snapshot->values, BASELINE_VALUES) ^ snapshot->cmos_crc ^ snapshot->backup_crc;
}

int baseline_difference(const baseline_snapshot_t* reference, const baseline_snapshot_t* now, uint16_t* port,
    uint8_t* before, uint8_t* after)
{
    for (unsigned i = 0; i < COUNT_OF(k_registers); i++)
    {
        if (reference->values[i] != now->values[i])
        {
            *port = k_registers[i].port;
            *before = reference->values[i];
            *after = now->values[i];
            return 1;
        }
    }

    for (int i = BASELINE_VALUES - 4; i < BASELINE_VALUES; i++)
    {
        if (reference->values[i] != now->values[i])
        {
            *port = (uint16_t)(i < BASELINE_VALUES - 2 ? 0x0000 : 0x0010);
            *before = reference->values[i];
            *after = now->values[i];
            return 1;
        }
    }

    if (reference->cmos_crc != now->cmos_crc)
    {
        *port = 0x3000;
        *before = 0;
        *after = 0;
        return 1;
    }

    if (reference->backup_crc != now->backup_crc)
    {
        *port = 0xF420;
        *before = 0;
        *after = 0;
        return 1;
    }

    return 0;
}

void baseline_ym_write(int bank, uint8_t reg, uint8_t value)
{
    uint16_t address = bank != 0 ? 0x04DC : 0x04D8;
    ym_wait();
    out8(address, reg);
    ym_wait();
    out8((uint16_t)(address + 2), value);
}

static void reset_pics(void)
{
    pic_init(g_machine.id);
}

static void reset_timers(void)
{
    tb_pit_program();
    out8(0x0046, 0x70);
    out8(0x0046, 0xB6);
    out8(0x0044, 0x00);
    out8(0x0044, 0x00);
    out8(0x0060, 0x80);

    if (g_machine.has_timer2)
    {
        out8(0x0068, 0x80);
        out8(0x006A, g_boot.port_006a);
        out8(0x006B, g_boot.port_006b);
        (void)in8(0x0068);
    }
}

static void reset_sound(void)
{
    static const uint8_t k_channels[] = { 0, 1, 2, 4, 5, 6 };

    for (unsigned i = 0; i < COUNT_OF(k_channels); i++)
        baseline_ym_write(0, 0x28, k_channels[i]);

    for (int bank = 0; bank < 2; bank++)
    {
        for (uint8_t reg = 0x40; reg < 0x50; reg++)
        {
            if ((reg & 0x03) != 0x03)
                baseline_ym_write(bank, reg, 0x7F);
        }

        for (uint8_t reg = 0x80; reg < 0x90; reg++)
        {
            if ((reg & 0x03) != 0x03)
                baseline_ym_write(bank, reg, 0xFF);
        }
    }

    baseline_ym_write(0, 0x27, 0x30);
    baseline_ym_write(0, 0x2B, 0x00);

    out8(0x04F8, 0xFF);

    for (uint8_t channel = 0; channel < 8; channel++)
    {
        out8(0x04F7, (uint8_t)(0x40 | channel));
        out8(0x04F0, 0x00);
    }

    for (uint8_t bank = 0; bank < PCM_BANKS; bank++)
    {
        out8(0x04F7, bank);
        volatile uint32_t* window = (volatile uint32_t*)g_machine.pcm_window;

        for (int i = 0; i < PCM_BANK_SIZE / 4; i++)
            window[i] = 0;
    }

    out8(0x04F7, 0x00);
}

static void reset_cdc(void)
{
    out8(0x04C0, 0xC0);

    for (int i = 0; i < 256 && (in8(0x04C0) & 0x02) != 0; i++)
        (void)in8(0x04C2);

    out8(0x04C0, 0xC0);
}

static void reset_floppy(void)
{
    out8(0x0200, 0xD0);
    out8(0x0208, 0x00);
    out8(0x020C, 0x00);
}

static void reset_keyboard(void)
{
    for (int i = 0; i < 256 && (in8(0x0602) & 0x01) != 0; i++)
        (void)in8(0x0600);

    out8(0x0604, 0x00);
}

static void reset_video(int screen_mode)
{
    static const uint8_t k_palettes[] = { 0x00, 0x20, 0x10 };

    out8(0x0450, 0x00);
    out8(0x0452, 0xFF);
    out8(0x0450, 0x01);
    out8(0x0452, 0x03);

    for (uint8_t reg = 2; reg <= 6; reg++)
    {
        out8(0x0450, reg);
        out8(0x0452, 0x00);
    }

    volatile uint32_t* sprites = (volatile uint32_t*)g_machine.sprite_ram;

    for (int i = 0; i < SPRITE_RAM_SIZE / 4; i++)
        sprites[i] = 0;

    for (unsigned bank = 0; bank < COUNT_OF(k_palettes); bank++)
    {
        int entries = k_palettes[bank] == 0x10 ? 256 : 16;
        out8(0x0448, 0x01);
        out8(0x044A, (uint8_t)(0x08 | k_palettes[bank]));

        for (int i = 0; i < entries; i++)
        {
            uint8_t level = (uint8_t)(entries == 16 ? i * 17 : i);
            out8(0xFD90, (uint8_t)i);
            out8(0xFD92, level);
            out8(0xFD94, level);
            out8(0xFD96, level);
        }
    }

    if ((g_boot.port_0404 & 0x80) == 0)
    {
        MMIO8(FMR_REGISTERS + 0x01) = 0x0F;
        MMIO8(FMR_REGISTERS + 0x02) = 0x27;
        MMIO8(FMR_REGISTERS + 0x03) = 0x00;
        MMIO8(FMR_REGISTERS + 0x18) = 0x00;
    }

    scr_init(screen_mode, g_machine.vram);
}

// Test scratch from 1 MiB to the end of RAM, up to 4 MiB
static void reset_scratch(void)
{
    uint32_t end = (uint32_t)g_machine.ram_mb << 20;

    if (end > 0x00400000u)
        end = 0x00400000u;

    for (uint32_t address = SCRATCH_BASE; address < end; address += 4)
        MMIO32(address) = 0;

    for (uint32_t address = TEST_STACK_BASE; address < TEST_STACK_TOP; address += 4)
        MMIO32(address) = 0;
}

static void ym_wait(void)
{
    uint32_t start = tb_now();

    while ((in8(0x04D8) & 0x80) != 0 && tb_now() - start < 1000)
        ;
}
