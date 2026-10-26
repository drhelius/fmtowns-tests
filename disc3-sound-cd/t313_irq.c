#include "tests.h"

// 3.13 Sound registers and IRQ13, silent: the mixer, PCM and sound interrupt ports as found and how they read back,
// the RF5C68's rate from its block interrupts (a channel stepping one sample a clock through all 64 KiB of wave RAM),
// and how the FM timer flags and the PCM causes reach 04E9h and IRQ13. Expected: 4 KiB blocks, 8 KiB per mask bit,
// 04EBh cleared by reading, IRQ13 = (FM flags) or (PCM causes), PCM at 8 MHz / 384. Also the ADC ports read 256 times,
// and wave RAM written and read through its window by byte, word and dword with the PCM stopped and running, every
// channel off; the rate step fills wave RAM by byte with the PCM stopped

#define FOUND_PORTS 20
#define EVENTS 16
#define BLOCK_TIMEOUT 1000000u
#define WAVE_SIZE 0x10000u
#define SILENT 0x80
#define ADC_READS 256
#define ADC_GAP_US 100
#define ADC_KEPT 16
#define WAVE_CASES 6
#define WAVE_OFFSET 0x0200u
#define PCM_RUN 0x80

typedef struct
{
    uint8_t found[FOUND_PORTS];
    uint8_t gate_reads[2];
    uint8_t mask_reads[2];
    uint8_t volume_reads[2];
    uint8_t adc_min;
    uint8_t adc_max;
    uint8_t ready_and;
    uint8_t ready_or;
    uint16_t ready_count;
    uint8_t adc_kept[ADC_KEPT];
    uint8_t wave_cases[WAVE_CASES][4];
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
static void step_adc(irq_t* result);
static void step_wave(irq_t* result);
static void wave_case(int which, uint8_t* reads);
static uint8_t irq13(void);
static uint32_t rate(const irq_t* result);

static const test_step_t k_steps[] =
{
    { "REGISTERS AS FOUND", 0 },
    { "PCM RATE FROM BLOCK IRQ", 0 },
    { "FM TIMER ON IRQ13", 0 },
    { "ADC PORTS", 0 },
    { "WAVE RAM ACCESS", 0 },
    { 0, 0 }
};

static const uint8_t k_wave_pattern[4] = { 0x12, 0x34, 0xA5, 0x5A };

static const char* const k_wave_names[WAVE_CASES] =
{
    "B OFF > B OFF", "B ON  > B OFF", "B OFF > B ON", "W OFF > B OFF", "D OFF > B OFF", "B OFF > W OFF"
};

static const uint16_t k_ports[FOUND_PORTS] =
{
    0x04D5, 0x04E0, 0x04E1, 0x04E2, 0x04E3, 0x04E7, 0x04E8, 0x04E9, 0x04EA, 0x04EB, 0x04EC,
    0x04F0, 0x04F1, 0x04F2, 0x04F3, 0x04F4, 0x04F5, 0x04F6, 0x04F7, 0x04F8
};

const test_t g_test_irq =
{
    "3.13", "SOUND REGS AND IRQ13", 0, TEST_RUN_ALL, 2, sizeof(irq_t), k_steps, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    irq_t* result = (irq_t*)data;

    if ((steps & 0x01) != 0)
        step_registers(result);

    if ((steps & 0x10) != 0)
        step_wave(result);

    if ((steps & 0x08) != 0)
        step_adc(result);

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
}

// 04E8h then 04E7h, 256 times 100 us apart: the data's range, how often bit 0 says ready, the first 16 samples
static void step_adc(irq_t* result)
{
    shell_step("ADC PORTS");
    result->adc_min = 0xFF;
    result->adc_max = 0x00;
    result->ready_and = 0xFF;
    result->ready_or = 0x00;

    for (int i = 0; i < ADC_READS; i++)
    {
        uint8_t ready = in8(0x04E8);
        uint8_t value = in8(0x04E7);

        result->ready_and &= ready;
        result->ready_or |= ready;
        result->ready_count = (uint16_t)(result->ready_count + (ready & 0x01));
        result->adc_min = value < result->adc_min ? value : result->adc_min;
        result->adc_max = value > result->adc_max ? value : result->adc_max;

        if (i < ADC_KEPT)
            result->adc_kept[i] = value;

        tb_delay(ADC_GAP_US);
    }
}

static void step_wave(irq_t* result)
{
    shell_step("WAVE RAM ACCESS");

    for (int i = 0; i < WAVE_CASES; i++)
        wave_case(i, result->wave_cases[i]);

    out8(0x04F7, 0x00);

    for (uint32_t i = 0; i < WAVE_CASES * 0x10u; i++)
        MMIO8(g_machine.pcm_window + WAVE_OFFSET + i) = SILENT;
}

// Four bytes of the pattern (XORed with the case) at their own offset in bank 0, written and read back by byte (B),
// word (W) or dword (D), with 04F7h's run bit off or on for each side
static void wave_case(int which, uint8_t* reads)
{
    uint32_t address = g_machine.pcm_window + WAVE_OFFSET + (uint32_t)which * 0x10u;
    uint8_t bytes[4];

    for (int i = 0; i < 4; i++)
        bytes[i] = (uint8_t)(k_wave_pattern[i] ^ which);

    out8(0x04F8, 0xFF);
    out8(0x04F7, which == 1 ? PCM_RUN : 0x00);

    if (which == 3)
    {
        MMIO16(address) = (uint16_t)(bytes[0] | (bytes[1] << 8));
        MMIO16(address + 2) = (uint16_t)(bytes[2] | (bytes[3] << 8));
    }
    else if (which == 4)
        MMIO32(address) = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
            ((uint32_t)bytes[3] << 24);
    else
    {
        for (int i = 0; i < 4; i++)
            MMIO8(address + (uint32_t)i) = bytes[i];
    }

    out8(0x04F7, which == 2 ? PCM_RUN : 0x00);

    if (which == 5)
    {
        uint16_t low = MMIO16(address);
        uint16_t high = MMIO16(address + 2);
        reads[0] = (uint8_t)low;
        reads[1] = (uint8_t)(low >> 8);
        reads[2] = (uint8_t)high;
        reads[3] = (uint8_t)(high >> 8);
    }
    else
    {
        for (int i = 0; i < 4; i++)
            reads[i] = MMIO8(address + (uint32_t)i);
    }

    out8(0x04F7, 0x00);
}

// Channel 0 at envelope 0 stepping 1.0 through silent wave RAM with no loop marker, every mask bit set
static void step_rate(irq_t* result)
{
    shell_step("PCM RATE: FILLING");
    out8(0x04EA, 0x00);
    (void)in8(0x04EB);

    for (uint32_t bank = 0; bank < WAVE_SIZE / 0x1000u; bank++)
    {
        out8(0x04F7, (uint8_t)bank);

        for (uint32_t i = 0; i < 0x1000u; i++)
            MMIO8(g_machine.pcm_window + i) = SILENT;
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
    return 3;
}

static void page(const void* data, uint32_t steps, int number)
{
    const irq_t* result = (const irq_t*)data;
    int row = SHELL_BODY_TOP;

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
        return;
    }

    if (number == 2)
    {
        shell_line(row++, COLOR_CYAN, "ADC: 04E8H, 04E7H, 256 READS");

        if ((steps & 0x08) == 0)
            shell_line(row++, COLOR_GRAY, "NOT RUN");
        else
        {
            shell_line(row++, COLOR_WHITE, "04E7H MIN %02X MAX %02X", result->adc_min, result->adc_max);
            shell_line(row++, COLOR_WHITE, "04E8H AND %02X OR %02X, READY %u", result->ready_and, result->ready_or,
                result->ready_count);

            for (int i = 0; i < ADC_KEPT; i += 8)
                shell_line(row++, COLOR_WHITE, " %02X %02X %02X %02X %02X %02X %02X %02X", result->adc_kept[i],
                    result->adc_kept[i + 1], result->adc_kept[i + 2], result->adc_kept[i + 3], result->adc_kept[i + 4],
                    result->adc_kept[i + 5], result->adc_kept[i + 6], result->adc_kept[i + 7]);
        }

        row++;
        shell_line(row++, COLOR_CYAN, "WAVE RAM, WRITTEN > READ, RUN BIT");

        if ((steps & 0x10) == 0)
        {
            shell_line(row++, COLOR_GRAY, "NOT RUN");
            return;
        }

        for (int i = 0; i < WAVE_CASES; i++)
        {
            const uint8_t* reads = result->wave_cases[i];
            int same = 1;
            int ones = 1;

            for (int k = 0; k < 4; k++)
            {
                same &= reads[k] == (uint8_t)(k_wave_pattern[k] ^ i);
                ones &= reads[k] == 0xFF;
            }

            shell_line(row++, same ? COLOR_WHITE : COLOR_YELLOW, "%-13s %02X %02X %02X %02X %s", k_wave_names[i], reads[0],
                reads[1], reads[2], reads[3], same ? "OK" : ones ? "FF" : "DIFF");
        }

        shell_line(row++, COLOR_GRAY, "B BYTE, W WORD, D DWORD; PATTERN 12 34");
        shell_line(row++, COLOR_GRAY, "A5 5A XORED WITH THE CASE NUMBER 0-5");
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
    shell_line(row++, COLOR_GRAY, "WAVE RAM FILLED BY BYTE, PCM STOPPED");

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
