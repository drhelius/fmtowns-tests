#include "checks.h"
#include "../disc1-cpu-board/t16_protect.c"

#define KEPT_BYTES 0x00000F00u

static const uint32_t k_straddle_word[STRADDLE_COUNT] =
{
    0x0030, 0x0060, 0x00C0, 0x0080, 0x0000, 0x0000, 0x0000, 0x1000, 0x3000
};

static const uint32_t k_straddle_dword[STRADDLE_COUNT] =
{
    0x00F0, 0x00E0, 0x00C0, 0x0080, 0x0000, 0x1000, 0x3000, 0x7000, 0xF000
};

static uint32_t kept_mask(const uint8_t* pattern, const uint8_t* map)
{
    uint32_t mask = 0;

    for (int i = 0; i < VECTORS; i++)
    {
        if (map[i] == pattern[i])
            mask |= 1u << i;
    }

    return mask;
}

static uint32_t written_mask(const uint8_t* expected, const uint8_t* map, int invert)
{
    uint32_t mask = 0;

    for (int i = 0; i < VECTORS; i++)
    {
        if (map[i] == (uint8_t)(invert ? ~expected[i] : expected[i]))
            mask |= 1u << i;
    }

    return mask;
}

static void check_straddle(const char* name, const uint32_t* got, const uint32_t* want)
{
    for (int i = 0; i < STRADDLE_COUNT; i++)
    {
        if (got[i] != want[i])
        {
            check_hex(name, got[i], want[i]);
            return;
        }
    }

    check_true(name, 1);
}

static void evaluate_protect(const void* data, uint32_t steps)
{
    const protect_t* result = (const protect_t*)data;
    (void)steps;

    check_hex("BYTES KEPT, 0020H = 88H", kept_mask(result->pattern, result->map_88), KEPT_BYTES);
    check_hex("BYTES WRITTEN, 0020H = 88H", written_mask(result->pattern, result->map_88, 1), ~KEPT_BYTES);
    check_hex("BYTES KEPT, 0020H = 80H", kept_mask(result->pattern, result->map_80), KEPT_BYTES);
    check_straddle("WORDS ACROSS THE KEPT BYTES", result->straddle_word, k_straddle_word);
    check_straddle("DWORDS ACROSS THE KEPT BYTES", result->straddle_dword, k_straddle_dword);
    check_value("SECTOR READ BY DMA", result->dma.result, CDC_OK);

    // A sector byte that equals the pattern can't tell a kept byte from a written one
    if (result->dma.result == CDC_OK)
    {
        uint32_t known = ~kept_mask(result->pattern, result->dma_sector);
        check_hex("BYTES KEPT FROM A DMA WRITE", kept_mask(result->pattern, result->dma_after) & known,
            KEPT_BYTES & known);
        check_hex("BYTES THE DMA WROTE", written_mask(result->dma_sector, result->dma_after, 0) & known,
            ~KEPT_BYTES & known);
    }
}

static void evaluate_nmi(const void* data, uint32_t steps)
{
    const protect_t* result = (const protect_t*)data;
    (void)steps;

    check_hex("0020H READS", result->read_0020, 0x00);
    check_hex("05C0H AT BOOT", result->boot_05c0, 0x00);
    check_hex("05C2H AT BOOT", result->boot_05c2, 0x00);
    check_hex("0028H", result->port_0028, 0xFE);
    check_hex("05C0H AFTER WRITING 00H", result->read_05c0[0], 0x00);
    check_hex("05C0H AFTER WRITING 08H", result->read_05c0[1], 0x08);
    check_hex("05C0H AFTER WRITING F7H", result->read_05c0[2], 0x00);
    check_hex("05C2H AFTER THE 00H WRITE", result->read_05c2[0], 0x00);
    check_hex("05C2H AFTER THE 08H WRITE", result->read_05c2[1], 0x00);
    check_hex("05C2H AFTER THE F7H WRITE", result->read_05c2[2], 0x00);
    check_hex("05C0H RESTORED", result->restored_05c0, 0x00);
}

static const test_t k_test = CHECK_TEST(g_test_protect);

const check_group_t g_check_protect = { "NMI VECTOR WRITE PROTECT", &k_test, 1, 0, evaluate_protect };
const check_group_t g_check_nmi = { "05C0H/05C2H NMI (MARTY)", &k_test, 1, check_marty, evaluate_nmi };
