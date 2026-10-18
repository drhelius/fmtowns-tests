#include "tests.h"

// 1.10 FPU results (docs/12, HT-38): transcendental and remainder instructions over 64 operands in each precision and
// rounding mode, one CRC-32 per combination of the 80-bit results and the status word, plus the raw results of the
// first 16 operands at 64-bit precision and round to nearest
// Runs only when an FPU answers, as 1.9 found it or, when 1.9 hasn't run, as FNINIT, FNSTSW and FNSTCW show; a machine
// without one only executes those three, which complete with no coprocessor

#define INSTRUCTIONS 11
#define PRECISIONS 3
#define ROUNDINGS 4
#define OPERANDS 64
#define RAW_OPERANDS 16
#define RESULT_SIZE 22
#define DIGEST_PAGES 4

typedef struct
{
    uint8_t ran;
    uint8_t fpu_present;
    uint8_t reserved[2];
    uint32_t digest[INSTRUCTIONS][PRECISIONS][ROUNDINGS];
    uint8_t raw[INSTRUCTIONS][RAW_OPERANDS][RESULT_SIZE];
} fpu_results_t;

extern void fpu_apply(int op, const void* first, const void* second, uint32_t control, void* out);
extern void fpu_operand(int kind, int k, void* out);

static const char* const k_names[INSTRUCTIONS] =
{
    "FSIN", "FCOS", "FSINCOS", "FPTAN", "FPATAN", "F2XM1", "FYL2X", "FYL2XP1", "FSCALE", "FPREM", "FPREM1"
};

static const uint8_t k_precisions[PRECISIONS] = { 0, 2, 3 };
static const char* const k_precision_names[PRECISIONS] = { "24", "53", "64" };

static uint8_t s_operands[OPERANDS][10];

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static void build_operands(void);
static void special(uint8_t* out, int negative, uint16_t exponent, uint32_t high, uint32_t low);

const test_t g_test_fpu =
{
    "1.10", "FPU RESULTS", "FPU", TEST_RUN_ALL, 2, sizeof(fpu_results_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    fpu_results_t* result = (fpu_results_t*)data;
    uint32_t identity_steps = 0;
    const void* identity = shell_result("1.9", &identity_steps);
    (void)steps;

    result->ran = identity != 0 && (identity_steps & 0x02) != 0 ? 1 : 2;
    result->fpu_present = (uint8_t)(result->ran == 1 ? fpu_id_present(identity) : fpu_detect());

    if (!result->fpu_present)
        return;

    write_cr0((read_cr0() & ~0x0Cu) | 0x02u);
    shell_step("OPERANDS");
    build_operands();

    for (int op = 0; op < INSTRUCTIONS; op++)
    {
        shell_step("%s", k_names[op]);

        for (int p = 0; p < PRECISIONS; p++)
        {
            for (int r = 0; r < ROUNDINGS; r++)
            {
                uint32_t control = 0x003Fu | ((uint32_t)k_precisions[p] << 8) | ((uint32_t)r << 10);
                uint32_t crc = 0xFFFFFFFFu;

                for (int i = 0; i < OPERANDS; i++)
                {
                    uint8_t out[24];
                    fpu_apply(op, s_operands[i], s_operands[(i * 7 + 3) % OPERANDS], control, out);
                    crc = crc32_update(crc, out, RESULT_SIZE);

                    if (k_precisions[p] == 3 && r == 0 && i < RAW_OPERANDS)
                        memcpy(result->raw[op][i], out, RESULT_SIZE);
                }

                result->digest[op][p][r] = ~crc;
            }
        }
    }
}

// Multiples of pi/4 and their neighbors one unit in the last place away, powers of two up to past the FSIN range,
// fractions, denormals, zeros, infinities and NaNs
static void build_operands(void)
{
    for (int i = 0; i < 32; i++)
    {
        fpu_operand(0, i / 2 - 8, s_operands[i]);

        if ((i & 1) != 0)
        {
            uint32_t low = (uint32_t)s_operands[i][0] | ((uint32_t)s_operands[i][1] << 8) |
                ((uint32_t)s_operands[i][2] << 16) | ((uint32_t)s_operands[i][3] << 24);
            low += (i & 2) != 0 ? 1 : 0xFFFFFFFFu;
            s_operands[i][0] = (uint8_t)low;
            s_operands[i][1] = (uint8_t)(low >> 8);
            s_operands[i][2] = (uint8_t)(low >> 16);
            s_operands[i][3] = (uint8_t)(low >> 24);
        }
    }

    static const int k_powers[8] = { 62, 63, 64, 100, -1, -20, 1000, -1000 };

    for (int i = 0; i < 8; i++)
        fpu_operand(1, k_powers[i], s_operands[32 + i]);

    for (int i = 0; i < 8; i++)
        fpu_operand(2, i * 5 - 17, s_operands[40 + i]);

    special(s_operands[48], 0, 0x0000, 0x00000000u, 0x00000001u);
    special(s_operands[49], 1, 0x0000, 0x40000000u, 0x00000000u);
    special(s_operands[50], 0, 0x0000, 0x7FFFFFFFu, 0xFFFFFFFFu);
    special(s_operands[51], 0, 0x0000, 0x80000000u, 0x00000000u);
    special(s_operands[52], 0, 0x0000, 0x00000000u, 0x00000000u);
    special(s_operands[53], 1, 0x0000, 0x00000000u, 0x00000000u);
    special(s_operands[54], 0, 0x7FFF, 0x80000000u, 0x00000000u);
    special(s_operands[55], 1, 0x7FFF, 0x80000000u, 0x00000000u);
    special(s_operands[56], 0, 0x7FFF, 0xC0000000u, 0x00000000u);
    special(s_operands[57], 0, 0x7FFF, 0xA0000000u, 0x00000000u);
    special(s_operands[58], 0, 0x3FFF, 0x80000000u, 0x00000000u);
    special(s_operands[59], 1, 0x3FFF, 0x80000000u, 0x00000000u);
    special(s_operands[60], 0, 0x3FFE, 0xFFFFFFFFu, 0xFFFFFFFFu);
    special(s_operands[61], 0, 0x403E, 0xFFFFFFFFu, 0xFFFFFFFFu);
    special(s_operands[62], 0, 0x0001, 0x80000000u, 0x00000000u);
    special(s_operands[63], 1, 0x3FFD, 0xB504F333u, 0xF9DE6484u);
}

static void special(uint8_t* out, int negative, uint16_t exponent, uint32_t high, uint32_t low)
{
    for (int i = 0; i < 4; i++)
    {
        out[i] = (uint8_t)(low >> (i * 8));
        out[4 + i] = (uint8_t)(high >> (i * 8));
    }

    out[8] = (uint8_t)exponent;
    out[9] = (uint8_t)((exponent >> 8) | (negative ? 0x80 : 0));
}

static int pages(const void* data, uint32_t steps)
{
    const fpu_results_t* result = (const fpu_results_t*)data;
    (void)steps;
    return result->fpu_present ? DIGEST_PAGES + INSTRUCTIONS : 1;
}

static void page(const void* data, uint32_t steps, int number)
{
    const fpu_results_t* result = (const fpu_results_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (!result->fpu_present)
    {
        shell_line(row++, COLOR_WHITE, "%s", result->ran == 1 ? "NO FPU (FROM 1.9)" : "NO FPU (FNINIT, FNSTSW, FNSTCW)");
        shell_line(row++, COLOR_GRAY, "HT-38 NEEDS A 387 CARD OR A 486DX");
        return;
    }

    if (number < DIGEST_PAGES)
    {
        shell_line(row++, COLOR_GRAY, "OP     PC RC0/RC2  RC1/RC3");

        for (int op = number * 3; op < number * 3 + 3 && op < INSTRUCTIONS; op++)
        {
            for (int p = 0; p < PRECISIONS; p++)
            {
                shell_line(row++, COLOR_WHITE, "%-7s%s %08X %08X", p == 0 ? k_names[op] : "", k_precision_names[p],
                    result->digest[op][p][0], result->digest[op][p][1]);
                shell_line(row++, COLOR_WHITE, "          %08X %08X", result->digest[op][p][2], result->digest[op][p][3]);
            }
        }

        return;
    }

    int op = number - DIGEST_PAGES;
    shell_line(row++, COLOR_CYAN, "%s PC 64 NEAREST, FIRST %u", k_names[op], RAW_OPERANDS);

    for (int i = 0; i < RAW_OPERANDS; i++)
    {
        const uint8_t* out = result->raw[op][i];
        shell_line(row++, COLOR_WHITE, "%2u %02X%02X %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X", i, out[21], out[20],
            out[9], out[8], out[7], out[6], out[5], out[4], out[3], out[2], out[1], out[0]);
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const fpu_results_t* result = (const fpu_results_t*)data;
    (void)steps;

    if (!result->fpu_present)
        fmt_format(line, 34, "%s", "NO FPU");
    else
        fmt_format(line, 34, "FSIN %08X", result->digest[0][2][0]);

    *color = COLOR_WHITE;
}
