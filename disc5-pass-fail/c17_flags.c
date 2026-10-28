#include "checks.h"
#include "../disc1-cpu-board/t17_flags.c"

#define DIGESTS 60

// The digests of every variant in k_variants order, the same in the three Marty 2 sessions
static const uint32_t k_digests[DIGESTS] =
{
    0x6408380Cu, 0x5F7675D3u, 0x33EAD4CDu, 0x017C1A7Bu, 0x26F91DC7u, 0xEB90CEBFu,
    0xA3FDE2E0u, 0x7E99AE2Du, 0xFA3B4DD2u, 0x51F0E62Bu, 0x9A13BF31u, 0xC7662C23u,
    0xEC74BF41u, 0xC8073F3Bu, 0xB7984056u, 0x61E3E224u, 0xF8D8AA3Du, 0xAAE0695Bu,
    0x24688B2Au, 0x081E7858u, 0x0D32C913u, 0xD5AF6BCFu, 0x70E30577u, 0xEA67EB0Du,
    0x32A7443Au, 0x14E24564u, 0x7597B115u, 0xEE4F99C6u, 0xC54B28FAu, 0xC6D607C5u,
    0x58F87FD4u, 0x19083ABCu, 0x3CF013CFu, 0x3B52F249u, 0x49FDFA77u, 0x8898743Eu,
    0x11E0E4B5u, 0xEC0D7E39u, 0x7868C73Au, 0xB0C6CBC5u, 0x7F1915D3u, 0xC4DA5A15u,
    0x442399AAu, 0x04F685D3u, 0xEB0D05C9u, 0xA5C59B64u, 0x976062A2u, 0xA1E5431Du,
    0xC4C7C2F3u, 0xE09DE8C9u, 0xAE890BB2u, 0x133FA5EEu, 0xFFD0B763u, 0x0979DC79u,
    0x0C629755u, 0xEA04E339u, 0x36ADC3C2u, 0x9F2C22CAu, 0x16769682u, 0xA595468Au
};

static void evaluate_division(const void* data, uint32_t steps)
{
    const flags_t* result = (const flags_t*)data;
    (void)steps;

    check_hex("5/2 DIV, AH (NOT CYRIX)", result->cyrix_ah, 0x97);

    for (int i = 0; i < DIV_VARIANTS; i++)
        check_value(k_div_names[i], result->div_matches[i], DIV_CASES * 2);

    check_hex("IDIV8 8080H/7FH #DE, FLAGS", result->idiv8_8080.flags_vector, 0x00010005);
    check_hex("IDIV16 9C2D5855H/47A5H, AX", result->idiv16_quirk.eax & 0xFFFF, 0x8000);
    check_hex("IDIV16 9C2D5855H/47A5H, DX", result->idiv16_quirk.edx & 0xFFFF, 0xD855);
    check_hex("IDIV16 9C2D5855H/47A5H, FLAGS", result->idiv16_quirk.flags_vector, 0x00000005);
}

static void evaluate_undefined(const void* data, uint32_t steps)
{
    const flags_t* result = (const flags_t*)data;
    uint32_t matching = 0;
    (void)steps;

    for (int i = 0; i < DIGESTS; i++)
        matching += result->digest[i] == k_digests[i];

    check_value("VARIANTS MATCHING THE MARTY", matching, DIGESTS);

    for (int i = 0; i < DIGESTS; i++)
    {
        if (result->digest[i] != k_digests[i])
            check_hex(k_variants[i].name, result->digest[i], k_digests[i]);
    }
}

static const test_t k_test = CHECK_TEST(g_test_flags);

const check_group_t g_check_division = { "DIV AND IDIV RESULTS, FLAGS", &k_test, 1, check_386, evaluate_division };
const check_group_t g_check_undefined = { "UNDEFINED FLAGS, 60 VARIANTS", &k_test, 1, check_386,
    evaluate_undefined };
