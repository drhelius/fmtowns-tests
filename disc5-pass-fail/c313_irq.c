#include "checks.h"
#define g_test_irq g_test_sound_irq
#include "../disc3-sound-cd/t313_irq.c"

#define STEP_REGISTERS 0x01
#define STEP_PCM 0x02
#define STEP_FM 0x04
#define STEP_ADC 0x08
#define STEP_WAVE 0x10

static const uint8_t k_block_flags[EVENTS] =
{
    0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x10, 0x10, 0x20, 0x20, 0x40, 0x40, 0x80, 0x80, 0x01, 0x01
};

static const uint8_t k_irq_pcm[4] = { 0, 0, 1, 0 };
static const uint8_t k_irq_fm[6] = { 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t k_wave[4][4] = { { 0x12, 0x34, 0xA5, 0x5A }, { 0x13, 0x35, 0xA4, 0x5B }, { 0x16, 0x00, 0xA1, 0x00 },
    { 0x17, 0x00, 0xA0, 0x00 } };

static int bytes_equal(const uint8_t* a, const uint8_t* b, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (a[i] != b[i])
            return 0;
    }

    return 1;
}

static void evaluate_registers(const void* data, uint32_t steps)
{
    const irq_t* result = (const irq_t*)data;
    int write_only = 1;
    (void)steps;

    for (int i = 11; i < FOUND_PORTS; i++)
        write_only &= result->found[i] == 0xFF;

    check_hex("04E2H AFTER WRITING 15H", result->volume_reads[0], 0xD5);
    check_hex("04E3H AFTER WRITING 07H", result->volume_reads[1], 0xE7);
    check_true("04E0H-04E3H UNUSED BITS SET", (result->found[1] & 0xC0) == 0xC0 && (result->found[2] & 0xE0) == 0xE0 &&
        (result->found[3] & 0xC0) == 0xC0 && (result->found[4] & 0xE0) == 0xE0);
    check_hex("04D5H READS BACK 00H", result->gate_reads[0], 0x00);
    check_hex("04D5H READS BACK 03H", result->gate_reads[1], 0x03);
    check_hex("04EAH READS BACK A5H", result->mask_reads[0], 0xA5);
    check_hex("04EAH READS BACK 00H", result->mask_reads[1], 0x00);
    check_true("04F0H-04F8H READ FFH", write_only);
    check_hex("04E9H IDLE", result->found[7], 0x00);
    check_hex("04EBH IDLE", result->found[9], 0x00);
}

// 4,096 samples a block at 20,833 Hz, one interrupt flag of 04EBh for every 8 KiB of wave RAM
static void evaluate_pcm(const void* data, uint32_t steps)
{
    const irq_t* result = (const irq_t*)data;
    uint32_t shortest = 0xFFFFFFFFu;
    uint32_t longest = 0;
    (void)steps;

    for (int i = 1; i < EVENTS && i < result->event_count; i++)
    {
        uint32_t interval = result->times[i] - result->times[i - 1];
        shortest = interval < shortest ? interval : shortest;
        longest = interval > longest ? interval : longest;
    }

    check_value("BLOCK WAITS TIMED OUT", result->timeouts, 0);
    check_value("BLOCK INTERRUPTS SEEN", result->event_count, EVENTS);
    check_hex("NO FLAGS WITH THE MASK 0", result->unmasked_flags, 0x00);
    check_true("IRQ13 FROM THE BLOCK FLAG", bytes_equal(result->irq_pcm, k_irq_pcm, 4));
    check_true("FLAGS STEP EVERY 8 KIB", bytes_equal(result->flags, k_block_flags, EVENTS));

    if (result->event_count >= 2)
    {
        check_range("SHORTEST BLOCK, US", shortest, 195600, 197600);
        check_range("LONGEST BLOCK, US", longest, 195600, 197600);
    }
}

static void evaluate_fm(const void* data, uint32_t steps)
{
    const irq_t* result = (const irq_t*)data;
    (void)steps;

    check_hex("TIMER A FLAG ON 04E9H", result->irq_fm[0], 0x01);
    check_value("TIMER A FLAG RAISES IRQ13", result->irq_fm[1], 1);
    check_true("CLEARED BY THE RESET, MASKED", bytes_equal(result->irq_fm + 2, k_irq_fm + 2, 4));
}

static void evaluate_adc(const void* data, uint32_t steps)
{
    const irq_t* result = (const irq_t*)data;
    (void)steps;

    check_value("04E8H DATA READY IN 256 READS", result->ready_count, ADC_READS);
    check_hex("04E8H READS 01H, AND", result->ready_and, 0x01);
    check_hex("04E8H READS 01H, OR", result->ready_or, 0x01);

    if (check_marty())
    {
        check_range("04E7H SILENT LEVEL, LOWEST", result->adc_min, 0x70, 0x90);
        check_range("04E7H SILENT LEVEL, HIGHEST", result->adc_max, 0x70, 0x90);
    }
}

static void evaluate_wave(const void* data, uint32_t steps)
{
    const irq_t* result = (const irq_t*)data;
    (void)steps;

    check_true("BYTES, PCM STOPPED", bytes_equal(result->wave_cases[0], k_wave[0], 4));
    check_true("BYTES WRITTEN WITH PCM RUNNING", bytes_equal(result->wave_cases[1], k_wave[1], 4));

    if (check_marty())
    {
        check_true("DWORD WRITE, EVEN BYTES ONLY", bytes_equal(result->wave_cases[4], k_wave[2], 4));
        check_true("WORD READ, EVEN BYTES ONLY", bytes_equal(result->wave_cases[5], k_wave[3], 4));
    }
}

static const test_t k_test = CHECK_TEST(g_test_sound_irq);

const check_group_t g_check_mixer = { "SOUND REGISTER READS", &k_test, STEP_REGISTERS, 0,
    evaluate_registers };
const check_group_t g_check_pcm = { "PCM BLOCK IRQ AND RATE", &k_test, STEP_PCM, 0, evaluate_pcm };
const check_group_t g_check_fm_irq = { "FM TIMER ON IRQ13", &k_test, STEP_FM, 0, evaluate_fm };
const check_group_t g_check_adc = { "SOUND INPUT ADC PORTS", &k_test, STEP_ADC, 0, evaluate_adc };
const check_group_t g_check_wave = { "PCM WAVE RAM WINDOW", &k_test, STEP_WAVE, 0, evaluate_wave };
