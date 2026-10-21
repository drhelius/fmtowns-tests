#include "tests.h"

// 3.13 Sound registers and IRQ13, silent: the mixer, PCM and sound interrupt ports as found and how they read back,
// the RF5C68's rate from its block interrupts (a channel stepping one sample a clock through all 64 KiB of wave RAM),
// and how the FM timer flags and the PCM causes reach 04E9h and IRQ13. Expected: 4 KiB blocks, 8 KiB per mask bit,
// 04EBh cleared by reading, IRQ13 = (FM flags) or (PCM causes), PCM at 8 MHz / 384

#define FOUND_PORTS 20
#define EVENTS 16
#define BLOCK_TIMEOUT 1000000u
#define WAVE_SIZE 0x10000u
#define SILENT 0x80

typedef struct
{
    uint8_t found[FOUND_PORTS];
    uint8_t gate_reads[2];
    uint8_t mask_reads[2];
    uint8_t volume_reads[2];
    uint8_t wave_reads[4];
    uint8_t unmasked_flags;
    uint8_t event_count;
    uint8_t timeouts;
    uint8_t reserved;
    uint32_t times[EVENTS];
    uint8_t flags[EVENTS];
    uint8_t irq_pcm[4];
    uint8_t irq_fm[6];
    uint8_t reserved2[2];
} irq_t;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void step_registers(irq_t* result);
static void step_rate(irq_t* result);
static void step_fm(irq_t* result);
static uint8_t irq13(void);
static uint32_t rate(const irq_t* result);

static const test_step_t k_steps[] =
{
    { "REGISTERS AS FOUND", 0 },
    { "PCM RATE FROM BLOCK IRQ", 0 },
    { "FM TIMER ON IRQ13", 0 },
    { 0, 0 }
};

static const uint16_t k_ports[FOUND_PORTS] =
{
    0x04D5, 0x04E0, 0x04E1, 0x04E2, 0x04E3, 0x04E7, 0x04E8, 0x04E9, 0x04EA, 0x04EB, 0x04EC,
    0x04F0, 0x04F1, 0x04F2, 0x04F3, 0x04F4, 0x04F5, 0x04F6, 0x04F7, 0x04F8
};

const test_t g_test_irq =
{
    "3.13", "SOUND REGS AND IRQ13", 0, TEST_RUN_ALL, 1, sizeof(irq_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    irq_t* result = (irq_t*)data;

    if ((steps & 0x01) != 0)
        step_registers(result);

    if ((steps & 0x02) != 0)
        step_rate(result);

    if ((steps & 0x04) != 0)
        step_fm(result);

    out8(0x04EA, 0x00);
    (void)in8(0x04EB);
    out8(0x04F8, 0xFF);
    out8(0x04F7, 0x00);
    snd_ym(0, 0x27, 0x30);
}

static void step_registers(irq_t* result)
{
    static const uint8_t k_pattern[4] = { 0x12, 0x34, 0xA5, 0x5A };
    uint8_t gate = in8(0x04D5);
    uint8_t command = in8(0x04E3);
    uint8_t volume = in8(0x04E2);

    shell_step("REGISTERS AS FOUND");

    for (int i = 0; i < FOUND_PORTS; i++)
        result->found[i] = in8(k_ports[i]);

    out8(0x04D5, 0x00);
    result->gate_reads[0] = in8(0x04D5);
    out8(0x04D5, 0x03);
    result->gate_reads[1] = in8(0x04D5);
    out8(0x04D5, gate);

    out8(0x04EA, 0xA5);
    result->mask_reads[0] = in8(0x04EA);
    out8(0x04EA, 0x00);
    result->mask_reads[1] = in8(0x04EA);

    out8(0x04E3, 0x07);
    out8(0x04E2, 0x15);
    result->volume_reads[0] = in8(0x04E2);
    result->volume_reads[1] = in8(0x04E3);
    out8(0x04E3, (uint8_t)(0x03 | (command & 0x1C)));
    out8(0x04E2, volume);
    out8(0x04E3, command);

    snd_pcm_write(0x0100, k_pattern, 4);
    out8(0x04F7, 0x00);

    for (int i = 0; i < 4; i++)
        result->wave_reads[i] = MMIO8(g_machine.pcm_window + 0x0100 + (uint32_t)i);

    snd_pcm_fill(0x0100, 0x00, 4);
}

// Channel 0 at envelope 0 stepping 1.0 through silent wave RAM with no loop marker, every mask bit set
static void step_rate(irq_t* result)
{
    shell_step("PCM RATE: FILLING");
    out8(0x04EA, 0x00);
    (void)in8(0x04EB);

    for (uint32_t bank = 0; bank < WAVE_SIZE / 0x1000u; bank++)
    {
        out8(0x04F7, (uint8_t)(0x80 | bank));

        for (uint32_t i = 0; i < 0x1000u; i += 4)
            MMIO32(g_machine.pcm_window + i) = 0x80808080u;
    }

    snd_pcm_channel(0, 0x00, 0x0000, 0x0800, 0x00, 0x00);
    snd_pcm_on(0x01);
    shell_step("PCM RATE: MASKED");
    tb_delay(500000u);
    result->unmasked_flags = in8(0x04EB);
    result->irq_pcm[0] = in8(0x04E9);
    result->irq_pcm[1] = irq13();
    out8(0x04EA, 0xFF);
    (void)in8(0x04EB);
    shell_step("PCM RATE: BLOCKS");

    for (int i = 0; i < EVENTS; i++)
    {
        uint32_t start = tb_now();
        uint8_t flags = 0;

        while (((flags = in8(0x04E9)) & 0x08) == 0 && tb_now() - start < BLOCK_TIMEOUT)
            ;

        if ((flags & 0x08) == 0)
        {
            result->timeouts++;
            break;
        }

        result->times[i] = tb_now();

        if (i == 0)
        {
            result->irq_pcm[2] = irq13();
            result->flags[i] = in8(0x04EB);
            result->irq_pcm[3] = irq13();
        }
        else
            result->flags[i] = in8(0x04EB);

        result->event_count = (uint8_t)(i + 1);
    }

    out8(0x04F8, 0xFF);
    out8(0x04EA, 0x00);
    (void)in8(0x04EB);
}

// 04E9h and IRQ13: timer A overflowed with its flag enabled, after the flag reset, overflowed with the flag disabled
static void step_fm(irq_t* result)
{
    shell_step("FM TIMER ON IRQ13");
    snd_ym(0, 0x27, 0x30);
    snd_ym(0, 0x24, 0xFA);
    snd_ym(0, 0x25, 0x00);
    snd_ym(0, 0x27, 0x05);
    tb_delay(2000);
    result->irq_fm[0] = in8(0x04E9);
    result->irq_fm[1] = irq13();
    snd_ym(0, 0x27, 0x15);
    snd_ym(0, 0x27, 0x00);
    result->irq_fm[2] = in8(0x04E9);
    result->irq_fm[3] = irq13();
    snd_ym(0, 0x27, 0x01);
    tb_delay(2000);
    result->irq_fm[4] = in8(0x04E9);
    result->irq_fm[5] = irq13();
    snd_ym(0, 0x27, 0x30);
}

static uint8_t irq13(void)
{
    return (uint8_t)((pic_read_irr() >> 13) & 1);
}

// Samples a second from the mean block interval, a block being 4,096 samples at a step of 1.0
static uint32_t rate(const irq_t* result)
{
    if (result->event_count < 3)
        return 0;

    uint32_t span = result->times[result->event_count - 1] - result->times[1];
    uint32_t blocks = (uint32_t)result->event_count - 2;
    return span >= 100 ? 4096u * blocks * 10000u / (span / 100u) : 0;
}

static int pages(const void* data, uint32_t steps)
{
    (void)data;
    (void)steps;
    return 2;
}

static void page(const void* data, uint32_t steps, int number)
{
    const irq_t* result = (const irq_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, COLOR_CYAN, "AS FOUND");

        for (int i = 0; i < FOUND_PORTS; i += 4)
            shell_line(row++, COLOR_WHITE, "%04X %02X  %04X %02X  %04X %02X  %04X %02X", k_ports[i], result->found[i],
                k_ports[i + 1], result->found[i + 1], k_ports[i + 2], result->found[i + 2], k_ports[i + 3],
                result->found[i + 3]);

        row++;
        shell_line(row++, COLOR_CYAN, "WRITTEN, THEN READ");
        shell_line(row++, COLOR_WHITE, "04D5H 00>%02X 03>%02X", result->gate_reads[0], result->gate_reads[1]);
        shell_line(row++, COLOR_WHITE, "04EAH A5>%02X 00>%02X", result->mask_reads[0], result->mask_reads[1]);
        shell_line(row++, COLOR_WHITE, "04E3H 07, 04E2H 15: %02X %02X", result->volume_reads[0],
            result->volume_reads[1]);
        shell_line(row++, COLOR_WHITE, "WAVE RAM 12 34 A5 5A: %02X %02X %02X %02X", result->wave_reads[0],
            result->wave_reads[1], result->wave_reads[2], result->wave_reads[3]);
        return;
    }

    shell_line(row++, COLOR_CYAN, "PCM BLOCK IRQ, STEP 1.0");
    shell_line(row++, COLOR_WHITE, "MASK 0: 04EBH %02X 04E9H %02X IRQ13 %u", result->unmasked_flags,
        result->irq_pcm[0], result->irq_pcm[1]);
    shell_line(row++, COLOR_WHITE, "FIRST: IRQ13 %u, AFTER 04EBH READ %u", result->irq_pcm[2], result->irq_pcm[3]);

    for (int i = 1; i < result->event_count && i < EVENTS; i += 3)
    {
        char text[3][12];

        for (int k = 0; k < 3; k++)
        {
            text[k][0] = 0;

            if (i + k < result->event_count)
                fmt_format(text[k], sizeof(text[k]), "%02X %6u", result->flags[i + k],
                    result->times[i + k] - result->times[i + k - 1]);
        }

        shell_line(row++, COLOR_WHITE, "%-10s %-10s %s", text[0], text[1], text[2]);
    }

    shell_line(row++, COLOR_GREEN, "%u SAMPLES/S (8 MHZ/384 20833)", rate(result));

    if (result->timeouts != 0)
        shell_line(row++, COLOR_YELLOW, "NO BLOCK IRQ IN 1 S");

    row++;
    shell_line(row++, COLOR_CYAN, "FM TIMER A: 04E9H, IRQ13");
    shell_line(row++, COLOR_WHITE, "FLAG ON  %02X %u", result->irq_fm[0], result->irq_fm[1]);
    shell_line(row++, COLOR_WHITE, "RESET    %02X %u", result->irq_fm[2], result->irq_fm[3]);
    shell_line(row++, COLOR_WHITE, "NO ENABLE %02X %u", result->irq_fm[4], result->irq_fm[5]);
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const irq_t* result = (const irq_t*)data;
    (void)steps;

    fmt_format(line, 34, "PCM %u HZ FM IRQ %02X", rate(result), result->irq_fm[0]);
    *color = result->timeouts != 0 ? COLOR_YELLOW : COLOR_WHITE;
}
