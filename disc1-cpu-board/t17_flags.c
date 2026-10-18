#include "tests.h"

// 1.7 Integer flags and results (docs/12, HT-25, HT-37): DIV and IDIV against the loop models of HT-25, and CRC-32
// digests of the results and arithmetic flags of instructions whose flags or results the manuals leave undefined,
// each over 64 operands from flags 0002h and 08D7h, with a digest per eight operands to narrow a difference down.
// Raw samples keep the flags, the vector and AX of a few of those executions per variant, for the QR only

#define FLAGS_MASK 0x08D5u
#define OPERANDS 64
#define SLICES 8
#define COUNTS 34
#define DIV_VARIANTS 6
#define DIV_CASES 64
#define DIV_MISMATCHES 2
#define DIGESTS_PER_PAGE 44

#define RAW_COUNTS 12
#define RAW_SHIFT_OPERANDS 2
#define RAW_PLAIN_OPERANDS 12
#define RAW_CAPACITY 2040
#define KIND_PLAIN 0
#define KIND_SHIFT 1
#define KIND_FAULT 2

typedef struct
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t eflags;
} alu_regs_t;

typedef struct
{
    const char* name;
    void (*stub)(void);
    uint8_t kind;
    uint8_t width;
} variant_t;

typedef struct
{
    uint32_t eax_in;
    uint32_t ebx_in;
    uint32_t ecx_in;
    uint32_t edx_in;
    uint32_t flags_in;
    uint32_t eax;
    uint32_t ebx;
    uint32_t edx;
    uint32_t flags_vector;
} case_row_t;

typedef struct
{
    void (*stub)(void);
    alu_regs_t* regs;
} exec_t;

extern void alu_exec(void (*stub)(void), alu_regs_t* regs);
extern void alu_cyrix(void), alu_div8(void), alu_div16(void), alu_div32(void), alu_idiv8(void), alu_idiv16(void);
extern void alu_idiv32(void), alu_mul8(void), alu_mul16(void), alu_mul32(void), alu_imul8(void), alu_imul16(void);
extern void alu_imul32(void), alu_imul2_16(void), alu_imul2_32(void), alu_imul3_16(void), alu_imul3_32(void);
extern void alu_bsf16(void), alu_bsf32(void), alu_bsr16(void), alu_bsr32(void);
extern void alu_shl8(void), alu_shl16(void), alu_shl32(void), alu_shr8(void), alu_shr16(void), alu_shr32(void);
extern void alu_sar8(void), alu_sar16(void), alu_sar32(void), alu_rol8(void), alu_rol16(void), alu_rol32(void);
extern void alu_ror8(void), alu_ror16(void), alu_ror32(void), alu_rcl8(void), alu_rcl16(void), alu_rcl32(void);
extern void alu_rcr8(void), alu_rcr16(void), alu_rcr32(void), alu_shld16(void), alu_shld32(void);
extern void alu_shrd16(void), alu_shrd32(void), alu_aaa(void), alu_aas(void), alu_daa(void), alu_das(void);
extern void alu_aam10(void), alu_aam16(void), alu_aam7(void), alu_aam0(void), alu_aad10(void), alu_aad16(void);
extern void alu_aad7(void), alu_bt16(void), alu_bt32(void), alu_bts16(void), alu_bts32(void), alu_btr16(void);
extern void alu_btr32(void), alu_btc16(void), alu_btc32(void), alu_bswap32(void), alu_bswap16(void);

static const variant_t k_variants[] =
{
    { "MUL8", alu_mul8, KIND_PLAIN, 8 }, { "MUL16", alu_mul16, KIND_PLAIN, 16 },
    { "MUL32", alu_mul32, KIND_PLAIN, 32 }, { "IMUL8", alu_imul8, KIND_PLAIN, 8 },
    { "IMUL16", alu_imul16, KIND_PLAIN, 16 }, { "IMUL32", alu_imul32, KIND_PLAIN, 32 },
    { "IMUL2W", alu_imul2_16, KIND_PLAIN, 16 }, { "IMUL2D", alu_imul2_32, KIND_PLAIN, 32 },
    { "IMUL3W", alu_imul3_16, KIND_PLAIN, 16 }, { "IMUL3D", alu_imul3_32, KIND_PLAIN, 32 },
    { "BSF16", alu_bsf16, KIND_PLAIN, 16 }, { "BSF32", alu_bsf32, KIND_PLAIN, 32 },
    { "BSR16", alu_bsr16, KIND_PLAIN, 16 }, { "BSR32", alu_bsr32, KIND_PLAIN, 32 },
    { "SHL8", alu_shl8, KIND_SHIFT, 8 }, { "SHL16", alu_shl16, KIND_SHIFT, 16 },
    { "SHL32", alu_shl32, KIND_SHIFT, 32 }, { "SHR8", alu_shr8, KIND_SHIFT, 8 },
    { "SHR16", alu_shr16, KIND_SHIFT, 16 }, { "SHR32", alu_shr32, KIND_SHIFT, 32 },
    { "SAR8", alu_sar8, KIND_SHIFT, 8 }, { "SAR16", alu_sar16, KIND_SHIFT, 16 },
    { "SAR32", alu_sar32, KIND_SHIFT, 32 }, { "ROL8", alu_rol8, KIND_SHIFT, 8 },
    { "ROL16", alu_rol16, KIND_SHIFT, 16 }, { "ROL32", alu_rol32, KIND_SHIFT, 32 },
    { "ROR8", alu_ror8, KIND_SHIFT, 8 }, { "ROR16", alu_ror16, KIND_SHIFT, 16 },
    { "ROR32", alu_ror32, KIND_SHIFT, 32 }, { "RCL8", alu_rcl8, KIND_SHIFT, 8 },
    { "RCL16", alu_rcl16, KIND_SHIFT, 16 }, { "RCL32", alu_rcl32, KIND_SHIFT, 32 },
    { "RCR8", alu_rcr8, KIND_SHIFT, 8 }, { "RCR16", alu_rcr16, KIND_SHIFT, 16 },
    { "RCR32", alu_rcr32, KIND_SHIFT, 32 }, { "SHLD16", alu_shld16, KIND_SHIFT, 16 },
    { "SHLD32", alu_shld32, KIND_SHIFT, 32 }, { "SHRD16", alu_shrd16, KIND_SHIFT, 16 },
    { "SHRD32", alu_shrd32, KIND_SHIFT, 32 }, { "AAA", alu_aaa, KIND_PLAIN, 8 },
    { "AAS", alu_aas, KIND_PLAIN, 8 }, { "DAA", alu_daa, KIND_PLAIN, 8 },
    { "DAS", alu_das, KIND_PLAIN, 8 }, { "AAM", alu_aam10, KIND_PLAIN, 8 },
    { "AAM 16", alu_aam16, KIND_PLAIN, 8 }, { "AAM 7", alu_aam7, KIND_PLAIN, 8 },
    { "AAM 0", alu_aam0, KIND_FAULT, 8 }, { "AAD", alu_aad10, KIND_PLAIN, 8 },
    { "AAD 16", alu_aad16, KIND_PLAIN, 8 }, { "AAD 7", alu_aad7, KIND_PLAIN, 8 },
    { "BT16", alu_bt16, KIND_PLAIN, 16 }, { "BT32", alu_bt32, KIND_PLAIN, 32 },
    { "BTS16", alu_bts16, KIND_PLAIN, 16 }, { "BTS32", alu_bts32, KIND_PLAIN, 32 },
    { "BTR16", alu_btr16, KIND_PLAIN, 16 }, { "BTR32", alu_btr32, KIND_PLAIN, 32 },
    { "BTC16", alu_btc16, KIND_PLAIN, 16 }, { "BTC32", alu_btc32, KIND_PLAIN, 32 },
    { "BSWAP", alu_bswap32, KIND_FAULT, 32 }, { "BSWAP66", alu_bswap16, KIND_FAULT, 16 }
};

#define VARIANTS ((int)COUNT_OF(k_variants))
#define VARIANT_CAPACITY 64

static void (*const k_div_stubs[DIV_VARIANTS])(void) =
{
    alu_div8, alu_div16, alu_div32, alu_idiv8, alu_idiv16, alu_idiv32
};

static const char* const k_div_names[DIV_VARIANTS] = { "DIV8", "DIV16", "DIV32", "IDIV8", "IDIV16", "IDIV32" };

static const uint32_t k_starts[2] = { 0x00000002u, 0x000008D7u };

static const uint32_t k_edges[16] =
{
    0x00000000u, 0x00000001u, 0x00000002u, 0x0000007Fu, 0x00000080u, 0x000000FFu, 0x00000100u, 0x00007FFFu,
    0x00008000u, 0x0000FFFFu, 0x00010000u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0x55555555u, 0xAAAAAAAAu
};

typedef struct
{
    uint8_t cyrix_ah;
    uint8_t variant_count;
    uint8_t reserved[2];
    uint16_t div_matches[DIV_VARIANTS];
    uint8_t div_mismatch_count[DIV_VARIANTS];
    uint32_t div_digest[DIV_VARIANTS];
    case_row_t div_mismatch[DIV_VARIANTS][DIV_MISMATCHES];
    case_row_t idiv8_8080;
    case_row_t idiv16_quirk;
    uint32_t digest[VARIANT_CAPACITY];
    uint32_t slice[VARIANT_CAPACITY][SLICES];
    uint16_t raw_rows;
    uint16_t reserved2;
    uint16_t raw[RAW_CAPACITY][2];
} flags_t;

static uint32_t s_random;

static void run(void* data, uint32_t steps);
static int pages(const void* data, uint32_t steps);
static void page(const void* data, uint32_t steps, int number);
static void summary(const void* data, uint32_t steps, char* line, uint8_t* color);
static uint32_t next_random(void);
static void exec_caught(void* argument);
static uint32_t execute(void (*stub)(void), int may_fault, alu_regs_t* regs);
static void operands(int variant, uint32_t* eax, uint32_t* ebx, uint32_t* edx);
static uint32_t run_variant(int variant, uint32_t* slices);
static void run_division(flags_t* result);
static void raw_samples(flags_t* result);
static void division_case(int variant, int index, uint32_t* high, uint32_t* low, uint32_t* divisor);
static int model_division(int variant, uint32_t high, uint32_t low, uint32_t divisor, uint32_t* quotient,
    uint32_t* remainder, uint32_t* flags);
static uint32_t arithmetic_flags(uint32_t a, uint32_t b, int width, int subtract);

const test_t g_test_flags =
{
    "1.7", "INTEGER FLAGS", 0, TEST_RUN_ALL, 2, sizeof(flags_t), 0, run, pages, page, summary
};

static void run(void* data, uint32_t steps)
{
    flags_t* result = (flags_t*)data;
    alu_regs_t regs = { 0, 0, 0, 0, 0, 0, 0x00000002u };
    (void)steps;

    shell_step("CYRIX CHECK");
    alu_exec(alu_cyrix, &regs);
    result->cyrix_ah = (uint8_t)(regs.eax >> 8);

    shell_step("DIV AND IDIV");
    run_division(result);

    result->variant_count = (uint8_t)VARIANTS;

    for (int variant = 0; variant < VARIANTS; variant++)
    {
        shell_step("%s", k_variants[variant].name);
        result->digest[variant] = run_variant(variant, result->slice[variant]);
    }

    shell_step("RAW SAMPLES");
    raw_samples(result);
}

// Shifts: operands 3 and 40 with counts 0 1 2 7 8 9 15 16 17 31 32 33; the rest: operands 0-7 and 40-43. Each from
// both flag starts, in that order
static void raw_samples(flags_t* result)
{
    static const uint8_t k_counts[RAW_COUNTS] = { 0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33 };
    static const uint8_t k_shift_operands[RAW_SHIFT_OPERANDS] = { 3, 40 };
    static const uint8_t k_plain_operands[RAW_PLAIN_OPERANDS] = { 0, 1, 2, 3, 4, 5, 6, 7, 40, 41, 42, 43 };
    int row = 0;

    for (int variant = 0; variant < VARIANTS; variant++)
    {
        const variant_t* entry = &k_variants[variant];
        int shift = entry->kind == KIND_SHIFT;
        int count = shift ? RAW_SHIFT_OPERANDS : RAW_PLAIN_OPERANDS;
        uint32_t eax[OPERANDS];
        uint32_t ebx[OPERANDS];
        uint32_t edx[OPERANDS];

        operands(variant, eax, ebx, edx);

        for (int k = 0; k < count; k++)
        {
            int i = shift ? k_shift_operands[k] : k_plain_operands[k];

            for (int c = 0; c < (shift ? RAW_COUNTS : 1); c++)
            {
                for (int start = 0; start < 2 && row < RAW_CAPACITY; start++)
                {
                    alu_regs_t regs = { eax[i], ebx[i], shift ? k_counts[c] : 0, edx[i], 0, 0, k_starts[start] };
                    uint32_t vector = execute(entry->stub, entry->kind == KIND_FAULT, &regs);
                    result->raw[row][0] = (uint16_t)((regs.eflags & FLAGS_MASK) | (vector << 12));
                    result->raw[row][1] = (uint16_t)regs.eax;
                    row++;
                }
            }
        }
    }

    result->raw_rows = (uint16_t)row;
}

static uint32_t next_random(void)
{
    s_random ^= s_random << 13;
    s_random ^= s_random >> 17;
    s_random ^= s_random << 5;
    return s_random;
}

static void exec_caught(void* argument)
{
    exec_t* exec = (exec_t*)argument;
    alu_exec(exec->stub, exec->regs);
}

// The exception vector plus one, or zero; a fault leaves the input registers and the flags the exception pushed
static uint32_t execute(void (*stub)(void), int may_fault, alu_regs_t* regs)
{
    if (!may_fault)
    {
        alu_exec(stub, regs);
        return 0;
    }

    cpu_exception_t exception;
    exec_t exec = { stub, regs };
    int vector = cpu_catch(exec_caught, &exec, &exception);

    if (vector > 0)
        regs->eflags = exception.eflags;

    return vector > 0 ? (uint32_t)vector : 0;
}

static void operands(int variant, uint32_t* eax, uint32_t* ebx, uint32_t* edx)
{
    s_random = 0x9E3779B9u ^ ((uint32_t)variant * 0x01000193u);

    for (int i = 0; i < OPERANDS; i++)
    {
        uint32_t a = next_random();
        uint32_t b = next_random();
        uint32_t d = next_random();
        eax[i] = i < 16 ? k_edges[i] ^ (a & 0xFFFF0000u & (i & 1 ? 0xFFFFFFFFu : 0)) : a;
        ebx[i] = i < 16 ? k_edges[(i * 7) & 15] : b;
        edx[i] = i < 16 ? k_edges[(i * 11) & 15] : d;
    }
}

// Executions in order: operand, then count (0-33 for shifts), then flag start. Each slice is eight operands
static uint32_t run_variant(int variant, uint32_t* slices)
{
    const variant_t* entry = &k_variants[variant];
    uint32_t eax[OPERANDS];
    uint32_t ebx[OPERANDS];
    uint32_t edx[OPERANDS];
    int counts = entry->kind == KIND_SHIFT ? COUNTS : 1;
    uint32_t total = 0xFFFFFFFFu;

    operands(variant, eax, ebx, edx);

    for (int slice = 0; slice < SLICES; slice++)
    {
        uint32_t crc = 0xFFFFFFFFu;

        for (int i = slice * (OPERANDS / SLICES); i < (slice + 1) * (OPERANDS / SLICES); i++)
        {
            for (int count = 0; count < counts; count++)
            {
                for (int start = 0; start < 2; start++)
                {
                    alu_regs_t regs = { eax[i], ebx[i], entry->kind == KIND_SHIFT ? (uint32_t)count : 0, edx[i], 0, 0,
                        k_starts[start] };
                    uint32_t vector = execute(entry->stub, entry->kind == KIND_FAULT, &regs);
                    uint32_t record[5] = { regs.eax, regs.ebx, regs.edx, regs.eflags & FLAGS_MASK, vector };
                    crc = crc32_update(crc, record, sizeof(record));
                    total = crc32_update(total, record, sizeof(record));
                }
            }
        }

        slices[slice] = ~crc;
    }

    return ~total;
}

static void run_division(flags_t* result)
{
    for (int variant = 0; variant < DIV_VARIANTS; variant++)
    {
        uint32_t total = 0xFFFFFFFFu;
        int width = 8 << (variant % 3);
        uint32_t mask = width == 32 ? 0xFFFFFFFFu : (1u << width) - 1;

        for (int index = 0; index < DIV_CASES; index++)
        {
            uint32_t high;
            uint32_t low;
            uint32_t divisor;
            division_case(variant, index, &high, &low, &divisor);

            for (int start = 0; start < 2; start++)
            {
                alu_regs_t regs = { 0, divisor, 0, 0, 0, 0, k_starts[start] };

                if (width == 8)
                    regs.eax = (high << 8) | low;
                else
                {
                    regs.eax = low;
                    regs.edx = high;
                }

                uint32_t vector = execute(k_div_stubs[variant], 1, &regs);
                uint32_t quotient = width == 8 ? regs.eax & 0xFF : regs.eax & mask;
                uint32_t remainder = width == 8 ? (regs.eax >> 8) & 0xFF : regs.edx & mask;
                uint32_t model_quotient;
                uint32_t model_remainder;
                uint32_t model_flags;
                int fault = model_division(variant, high, low, divisor, &model_quotient, &model_remainder,
                    &model_flags);
                int match = fault == (vector != 0) && (regs.eflags & FLAGS_MASK) == model_flags &&
                    (fault || (quotient == model_quotient && remainder == model_remainder));
                uint32_t record[5] = { regs.eax, regs.ebx, regs.edx, regs.eflags & FLAGS_MASK, vector };
                total = crc32_update(total, record, sizeof(record));

                if (match)
                    result->div_matches[variant]++;
                else if (result->div_mismatch_count[variant] < DIV_MISMATCHES)
                {
                    case_row_t* row = &result->div_mismatch[variant][result->div_mismatch_count[variant]++];
                    row->eax_in = width == 8 ? (high << 8) | low : low;
                    row->ebx_in = divisor;
                    row->ecx_in = (model_quotient & 0xFFFF) | (model_remainder << 16);
                    row->edx_in = width == 8 ? 0 : high;
                    row->flags_in = k_starts[start] | (model_flags << 16);
                    row->eax = regs.eax;
                    row->ebx = regs.ebx;
                    row->edx = regs.edx;
                    row->flags_vector = (regs.eflags & FLAGS_MASK) | (vector << 16);
                }

                if (start == 0 && variant == 3 && index == 0)
                {
                    result->idiv8_8080.eax_in = 0x8080;
                    result->idiv8_8080.ebx_in = 0x7F;
                    result->idiv8_8080.eax = regs.eax;
                    result->idiv8_8080.flags_vector = (regs.eflags & FLAGS_MASK) | (vector << 16);
                }

                if (start == 0 && variant == 4 && index == 0)
                {
                    result->idiv16_quirk.eax_in = low;
                    result->idiv16_quirk.edx_in = high;
                    result->idiv16_quirk.ebx_in = divisor;
                    result->idiv16_quirk.eax = regs.eax;
                    result->idiv16_quirk.edx = regs.edx;
                    result->idiv16_quirk.flags_vector = (regs.eflags & FLAGS_MASK) | (vector << 16);
                }
            }
        }

        result->div_digest[variant] = ~total;
    }
}

// Hand-picked cases first: HT-25's two predictions, zero, the largest quotient, exact and negative divisions and
// every fault class; then random operands, mostly in range
static void division_case(int variant, int index, uint32_t* high, uint32_t* low, uint32_t* divisor)
{
    static const uint32_t k_div8[][3] =
    {
        { 0x00, 0x00, 0x01 }, { 0xFE, 0xFF, 0xFF }, { 0x00, 0x64, 0x0A }, { 0x00, 0xFF, 0x01 }, { 0x01, 0x00, 0x01 },
        { 0x12, 0x34, 0x00 }, { 0x7F, 0xFF, 0x80 }, { 0x00, 0x05, 0x02 }, { 0xFF, 0xFF, 0xFF }, { 0x00, 0x01, 0xFF }
    };
    static const uint32_t k_idiv8[][3] =
    {
        { 0x80, 0x80, 0x7F }, { 0xFF, 0x80, 0x01 }, { 0x00, 0x80, 0xFF }, { 0x80, 0x00, 0xFF }, { 0xFF, 0x00, 0x02 },
        { 0x00, 0x00, 0x00 }, { 0xFF, 0xFF, 0x01 }, { 0xFF, 0xF6, 0x05 }, { 0x00, 0x0A, 0xFB }, { 0xFF, 0xF5, 0x05 },
        { 0x3F, 0x80, 0x7F }, { 0xC0, 0x80, 0x81 }
    };
    static const uint32_t k_div16[][3] =
    {
        { 0x0000, 0x0000, 0x0001 }, { 0xFFFE, 0xFFFF, 0xFFFF }, { 0x0000, 0x2710, 0x000A }, { 0x0001, 0x0000, 0x0001 },
        { 0x1234, 0x5678, 0x0000 }, { 0x0000, 0x0005, 0x0002 }
    };
    static const uint32_t k_idiv16[][3] =
    {
        { 0x9C2D, 0x5855, 0x47A5 }, { 0xFFFF, 0x8000, 0x0001 }, { 0x8000, 0x0000, 0xFFFF }, { 0x0000, 0x0000, 0x0000 },
        { 0xFFFF, 0xFFF6, 0x0005 }, { 0x0000, 0x000A, 0xFFFB }, { 0xFFFF, 0xFFF5, 0x0005 }
    };
    static const uint32_t k_div32[][3] =
    {
        { 0x00000000u, 0x00000000u, 0x00000001u }, { 0xFFFFFFFEu, 0xFFFFFFFFu, 0xFFFFFFFFu },
        { 0x00000001u, 0x00000000u, 0x00000001u }, { 0x12345678u, 0x9ABCDEF0u, 0x00000000u },
        { 0x00000000u, 0x00000005u, 0x00000002u }
    };
    static const uint32_t k_idiv32[][3] =
    {
        { 0xFFFFFFFFu, 0x80000000u, 0x00000001u }, { 0x80000000u, 0x00000000u, 0xFFFFFFFFu },
        { 0x00000000u, 0x00000000u, 0x00000000u }, { 0xFFFFFFFFu, 0xFFFFFFF6u, 0x00000005u },
        { 0x00000000u, 0x0000000Au, 0xFFFFFFFBu }, { 0xFFFFFFFFu, 0xFFFFFFF5u, 0x00000005u }
    };
    static const uint32_t (*const k_tables[DIV_VARIANTS])[3] = { k_div8, k_div16, k_div32, k_idiv8, k_idiv16, k_idiv32 };
    static const int k_sizes[DIV_VARIANTS] = { 10, 6, 5, 12, 7, 6 };
    int width = 8 << (variant % 3);
    uint32_t mask = width == 32 ? 0xFFFFFFFFu : (1u << width) - 1;

    if (index < k_sizes[variant])
    {
        *high = k_tables[variant][index][0];
        *low = k_tables[variant][index][1];
        *divisor = k_tables[variant][index][2];
        return;
    }

    s_random = 0x2545F491u ^ ((uint32_t)variant << 8) ^ (uint32_t)index;
    next_random();
    *divisor = next_random() & mask;

    if (*divisor == 0)
        *divisor = 3;

    *low = next_random() & mask;

    if (variant < 3)
        *high = (index & 7) == 0 ? next_random() & mask : (*divisor > 1 ? next_random() % *divisor : 0);
    else
        *high = (index & 1) ? ((next_random() & 3) == 0 ? mask : 0) : next_random() & mask;
}

// HT-25's models: a restoring DIV that faults after step w - 1 from high - divisor, and an IDIV loop on |N| or ~N
// that drops the bit shifted out of the partial remainder and checks the quotient at the end
static int model_division(int variant, uint32_t high, uint32_t low, uint32_t divisor, uint32_t* quotient,
    uint32_t* remainder, uint32_t* flags)
{
    int width = 8 << (variant % 3);
    uint32_t mask = width == 32 ? 0xFFFFFFFFu : (1u << width) - 1;

    *quotient = 0;
    *remainder = 0;

    if (variant < 3)
    {
        if (high >= divisor)
        {
            uint32_t partial = high - divisor;
            uint64_t shifted = 0;

            for (int i = 1; i < width; i++)
            {
                shifted = ((uint64_t)partial << 1) | ((low >> (width - i)) & 1);
                partial = (uint32_t)(shifted >= divisor ? shifted - divisor : shifted) & mask;
            }

            *flags = arithmetic_flags((uint32_t)shifted, divisor, width, 1);
            return 1;
        }

        uint32_t partial = high;
        uint32_t result = 0;
        uint64_t last = 0;

        for (int i = 0; i < width; i++)
        {
            uint64_t shifted = ((uint64_t)partial << 1) | ((low >> (width - 1 - i)) & 1);
            result <<= 1;
            last = shifted;

            if (shifted >= divisor)
            {
                partial = (uint32_t)(shifted - divisor);
                result |= 1;
            }
            else
                partial = (uint32_t)shifted;
        }

        *quotient = result & mask;
        *remainder = partial & mask;
        *flags = arithmetic_flags((uint32_t)last, divisor, width, 1);
        return 0;
    }

    uint64_t dividend = ((uint64_t)high << width) | low;
    int negative_dividend = (int)((dividend >> (2 * width - 1)) & 1);
    int negative_divisor = (int)((divisor >> (width - 1)) & 1);
    uint32_t magnitude = (negative_divisor ? 0 - divisor : divisor) & mask;
    uint64_t bits = negative_dividend ? ~dividend : dividend;
    uint32_t partial = (uint32_t)(bits >> width) & mask;
    uint32_t result = (uint32_t)bits & mask;

    for (int i = 0; i < width; i++)
    {
        uint32_t shifted = ((partial << 1) | (result >> (width - 1))) & mask;
        result = (result << 1) & mask;

        if (shifted >= magnitude)
        {
            partial = shifted - magnitude;
            result |= 1;
        }
        else
            partial = shifted;
    }

    if (negative_dividend)
        partial = ~partial & mask;

    *flags = arithmetic_flags(partial, divisor, width, negative_dividend == negative_divisor);

    uint64_t count = result;

    if (negative_dividend && partial == ((0 - magnitude) & mask))
    {
        partial = 0;
        count++;
    }

    int negative_quotient = negative_dividend != negative_divisor;
    uint64_t limit = (uint64_t)1 << (width - 1);

    if (magnitude == 0 || count > (negative_quotient ? limit : limit - 1))
        return 1;

    *quotient = (uint32_t)(negative_quotient ? 0 - count : count) & mask;
    *remainder = partial;
    return 0;
}

// OF SF ZF AF PF CF of a subtraction or an addition at the width
static uint32_t arithmetic_flags(uint32_t a, uint32_t b, int width, int subtract)
{
    uint32_t mask = width == 32 ? 0xFFFFFFFFu : (1u << width) - 1;
    uint32_t sign = 1u << (width - 1);
    uint32_t r = (subtract ? a - b : a + b) & mask;
    uint32_t flags = 0;
    int parity = 0;

    a &= mask;
    b &= mask;

    if (subtract ? a < b : (uint64_t)a + b > mask)
        flags |= 0x0001;

    for (int bit = 0; bit < 8; bit++)
        parity ^= (r >> bit) & 1;

    if (!parity)
        flags |= 0x0004;

    if (((a ^ b ^ r) & 0x10) != 0)
        flags |= 0x0010;

    if (r == 0)
        flags |= 0x0040;

    if ((r & sign) != 0)
        flags |= 0x0080;

    if ((subtract ? (a ^ b) & (a ^ r) : ~(a ^ b) & (a ^ r)) & sign)
        flags |= 0x0800;

    return flags;
}

static int pages(const void* data, uint32_t steps)
{
    const flags_t* result = (const flags_t*)data;
    (void)steps;
    return 1 + (result->variant_count + DIGESTS_PER_PAGE - 1) / DIGESTS_PER_PAGE;
}

static void page(const void* data, uint32_t steps, int number)
{
    const flags_t* result = (const flags_t*)data;
    int row = SHELL_BODY_TOP;
    (void)steps;

    if (number == 0)
    {
        shell_line(row++, result->cyrix_ah == 0x97 ? COLOR_GREEN : COLOR_YELLOW, "CYRIX CHECK AH %02X (INTEL 97, CYRIX 02)",
            result->cyrix_ah);
        row++;
        shell_line(row++, COLOR_CYAN, "AGAINST HT-25'S LOOP MODELS (386EX)");

        for (int variant = 0; variant < DIV_VARIANTS; variant++)
            shell_line(row++, result->div_matches[variant] == DIV_CASES * 2 ? COLOR_GREEN : COLOR_YELLOW,
                "%-6s %3u/%u %08X", k_div_names[variant], result->div_matches[variant], DIV_CASES * 2,
                result->div_digest[variant]);

        row++;
        shell_line(row++, COLOR_CYAN, "THE TWO PREDICTIONS");
        uint32_t vector = result->idiv8_8080.flags_vector >> 16;
        shell_line(row++, vector == 1 ? COLOR_GREEN : COLOR_YELLOW, "IDIV8 8080/7F %s (PREDICTED #DE)",
            vector == 1 ? "#DE" : "NO FAULT");
        shell_line(row++, COLOR_WHITE, " AX %04X FLAGS %04X", result->idiv8_8080.eax & 0xFFFF,
            result->idiv8_8080.flags_vector & 0xFFFF);
        int quirk = (result->idiv16_quirk.eax & 0xFFFF) == 0x8000 && (result->idiv16_quirk.edx & 0xFFFF) == 0xD855 &&
            (result->idiv16_quirk.flags_vector >> 16) == 0;
        shell_line(row++, quirk ? COLOR_GREEN : COLOR_YELLOW, "IDIV16 9C2D5855/47A5 Q %04X R %04X",
            result->idiv16_quirk.eax & 0xFFFF, result->idiv16_quirk.edx & 0xFFFF);
        shell_line(row++, COLOR_WHITE, " PREDICTED Q 8000 R D855, V%u", result->idiv16_quirk.flags_vector >> 16);
        shell_line(row++, COLOR_GRAY, "RAW SAMPLES: %u ROWS, IN THE QR ONLY", result->raw_rows);
        row++;

        for (int variant = 0; variant < DIV_VARIANTS && row < SHELL_BODY_BOTTOM; variant++)
        {
            for (int i = 0; i < result->div_mismatch_count[variant] && row < SHELL_BODY_BOTTOM; i++)
            {
                const case_row_t* entry = &result->div_mismatch[variant][i];
                shell_line(row++, COLOR_YELLOW, "%s %08X:%08X/%08X", k_div_names[variant], entry->edx_in,
                    entry->eax_in, entry->ebx_in);
                shell_line(row++, COLOR_WHITE, " GOT %08X %08X %04X V%u", entry->eax, entry->edx,
                    entry->flags_vector & 0xFFFF, entry->flags_vector >> 16);
            }
        }

        return;
    }

    int first = (number - 1) * DIGESTS_PER_PAGE;

    for (int variant = first; variant < first + DIGESTS_PER_PAGE && variant < result->variant_count; variant += 2)
    {
        if (variant + 1 < result->variant_count)
            shell_line(row++, COLOR_WHITE, "%-8s%08X  %-8s%08X", k_variants[variant].name, result->digest[variant],
                k_variants[variant + 1].name, result->digest[variant + 1]);
        else
            shell_line(row++, COLOR_WHITE, "%-8s%08X", k_variants[variant].name, result->digest[variant]);
    }
}

static void summary(const void* data, uint32_t steps, char* line, uint8_t* color)
{
    const flags_t* result = (const flags_t*)data;
    int matches = 0;
    (void)steps;

    for (int variant = 0; variant < DIV_VARIANTS; variant++)
        matches += result->div_matches[variant];

    fmt_format(line, 34, "AH %02X DIV %u/%u MUL8 %08X", result->cyrix_ah, matches, DIV_VARIANTS * DIV_CASES * 2,
        result->digest[0]);
    *color = matches != DIV_VARIANTS * DIV_CASES * 2 ? COLOR_YELLOW : COLOR_WHITE;
}
