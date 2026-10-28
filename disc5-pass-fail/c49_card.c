#include "checks.h"
#include "../disc4-input-storage/t49_card.c"

#define STEP_REGISTERS 0x01
#define BLANK_CRC 0xF154670Au

static void evaluate_card(const void* data, uint32_t steps)
{
    const card_t* result = (const card_t*)data;
    int blank = 1;
    (void)steps;

    for (int bank = 0; bank < BANKS; bank++)
    {
        blank &= result->bank_crc[bank] == BLANK_CRC;

        for (int i = 0; i < KEEP; i++)
            blank &= result->bank_bytes[bank][i] == 0xFF;
    }

    for (int i = 0; i < ATTRIBUTE; i++)
        blank &= result->attribute[i] == 0xFF;

    check_hex("048AH STATUS, NO CARD", result->status[0], 0x07);
    check_hex("048AH STATUS AGAIN", result->status[1], 0x07);
    check_hex("0490H READS BACK THE BANK", result->bank_reads[0], 0x01);
    check_hex("0491H READS BACK", result->bank_reads[1], 0x01);
    check_true("WINDOW AND ATTRIBUTES READ FFH", blank);
}

static const test_t k_test = CHECK_TEST(g_test_card);

const check_group_t g_check_card = { "IC CARD SLOT, NO CARD (MARTY)", &k_test, STEP_REGISTERS, check_marty,
    evaluate_card };
