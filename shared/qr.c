#include "qr.h"

#define DARK 0x01
#define FUNCTION 0x02
#define MAX_CODEWORDS 1156

static const uint8_t k_ecc_per_block[QR_MAX_VERSION + 1] =
{
    0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26
};

static const uint8_t k_blocks[QR_MAX_VERSION + 1] =
{
    0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17
};

static uint8_t s_data[MAX_CODEWORDS];
static uint8_t s_codewords[MAX_CODEWORDS];

static int raw_modules(int version);
static int data_codewords(int version);
static int count_bits(int version);
static int alphanumeric_value(char c);
static uint8_t gf_multiply(uint8_t x, uint8_t y);
static void add_ecc(int version);
static void set_function(qr_code_t* qr, int x, int y, int dark);
static void draw_function_patterns(qr_code_t* qr);
static void draw_format(qr_code_t* qr, int mask);
static void draw_codewords(qr_code_t* qr, int length);
static void apply_mask(qr_code_t* qr, int mask);
static int alignment_positions(int version, int* positions);

int qr_capacity(int version)
{
    int bits = data_codewords(version) * 8 - 4 - count_bits(version);
    return (bits / 11) * 2 + ((bits % 11) >= 6 ? 1 : 0);
}

int qr_largest_version(int pixels, int scale)
{
    int modules = pixels / scale - 2 * QR_QUIET_ZONE;
    int version = (modules - 17) / 4;
    return version > QR_MAX_VERSION ? QR_MAX_VERSION : version;
}

int qr_encode(qr_code_t* qr, const char* text, int length, int max_version, int mask)
{
    int version = 1;

    while (version <= max_version && qr_capacity(version) < length)
        version++;

    if (version > max_version)
        return -1;

    int capacity = data_codewords(version);
    int bit = 0;

    for (int i = 0; i < capacity; i++)
        s_data[i] = 0;

#define PUT_BITS(value, count) \
    for (int b = (count) - 1; b >= 0; b--, bit++) \
        s_data[bit >> 3] |= (uint8_t)((((value) >> b) & 1) << (7 - (bit & 7)))

    PUT_BITS(0x2, 4);
    PUT_BITS(length, count_bits(version));

    for (int i = 0; i + 1 < length; i += 2)
    {
        int pair = alphanumeric_value(text[i]) * 45 + alphanumeric_value(text[i + 1]);
        PUT_BITS(pair, 11);
    }

    if ((length & 1) != 0)
    {
        int last = alphanumeric_value(text[length - 1]);
        PUT_BITS(last, 6);
    }

#undef PUT_BITS

    int terminator = capacity * 8 - bit;
    bit += terminator > 4 ? 4 : terminator;
    bit = (bit + 7) & ~7;

    for (int i = bit >> 3, pad = 0xEC; i < capacity; i++, pad ^= 0xEC ^ 0x11)
        s_data[i] = (uint8_t)pad;

    add_ecc(version);

    qr->version = version;
    qr->size = version * 4 + 17;

    for (int y = 0; y < qr->size; y++)
    {
        for (int x = 0; x < qr->size; x++)
            qr->modules[y][x] = 0;
    }

    draw_function_patterns(qr);
    draw_codewords(qr, raw_modules(version) / 8);
    apply_mask(qr, mask);
    draw_format(qr, mask);
    qr->mask = mask;
    return 0;
}

static int raw_modules(int version)
{
    int result = (16 * version + 128) * version + 64;

    if (version >= 2)
    {
        int alignments = version / 7 + 2;
        result -= (25 * alignments - 10) * alignments - 55;

        if (version >= 7)
            result -= 36;
    }

    return result;
}

static int data_codewords(int version)
{
    return raw_modules(version) / 8 - k_ecc_per_block[version] * k_blocks[version];
}

static int count_bits(int version)
{
    return version <= 9 ? 9 : 11;
}

static int alphanumeric_value(char c)
{
    static const char k_symbols[] = " $%*+-./:";

    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;

    for (int i = 0; k_symbols[i] != 0; i++)
    {
        if (k_symbols[i] == c)
            return 36 + i;
    }

    return 0;
}

// Logarithm tables of GF(256) with the polynomial 11Dh and the generator 2
static uint8_t gf_multiply(uint8_t x, uint8_t y)
{
    static uint8_t s_exp[512];
    static uint8_t s_log[256];

    if (s_exp[0] == 0)
    {
        int value = 1;

        for (int i = 0; i < 255; i++)
        {
            s_exp[i] = (uint8_t)value;
            s_exp[i + 255] = (uint8_t)value;
            s_log[value] = (uint8_t)i;
            value <<= 1;

            if ((value & 0x100) != 0)
                value ^= 0x11D;
        }
    }

    if (x == 0 || y == 0)
        return 0;

    return s_exp[s_log[x] + s_log[y]];
}

static void add_ecc(int version)
{
    int blocks = k_blocks[version];
    int ecc_length = k_ecc_per_block[version];
    int raw = raw_modules(version) / 8;
    int data_length = raw - ecc_length * blocks;
    int short_blocks = blocks - raw % blocks;
    int short_length = raw / blocks - ecc_length;
    uint8_t generator[30];
    uint8_t remainder[30];
    uint8_t root = 1;
    const uint8_t* data = s_data;

    for (int i = 0; i < ecc_length; i++)
        generator[i] = 0;

    generator[ecc_length - 1] = 1;

    for (int i = 0; i < ecc_length; i++)
    {
        for (int j = 0; j < ecc_length; j++)
        {
            generator[j] = gf_multiply(generator[j], root);

            if (j + 1 < ecc_length)
                generator[j] ^= generator[j + 1];
        }

        root = gf_multiply(root, 0x02);
    }

    for (int i = 0; i < blocks; i++)
    {
        int length = short_length + (i < short_blocks ? 0 : 1);

        for (int j = 0; j < ecc_length; j++)
            remainder[j] = 0;

        for (int j = 0; j < length; j++)
        {
            uint8_t factor = data[j] ^ remainder[0];

            for (int k = 0; k + 1 < ecc_length; k++)
                remainder[k] = remainder[k + 1];

            remainder[ecc_length - 1] = 0;

            for (int k = 0; k < ecc_length; k++)
                remainder[k] ^= gf_multiply(generator[k], factor);
        }

        for (int j = 0, k = i; j < length; j++, k += blocks)
        {
            if (j == short_length)
                k -= short_blocks;

            s_codewords[k] = data[j];
        }

        for (int j = 0, k = data_length + i; j < ecc_length; j++, k += blocks)
            s_codewords[k] = remainder[j];

        data += length;
    }
}

static void set_function(qr_code_t* qr, int x, int y, int dark)
{
    qr->modules[y][x] = (uint8_t)(FUNCTION | (dark ? DARK : 0));
}

static void draw_function_patterns(qr_code_t* qr)
{
    int size = qr->size;
    int positions[7];
    int count = alignment_positions(qr->version, positions);

    for (int i = 0; i < size; i++)
    {
        set_function(qr, 6, i, (i & 1) == 0);
        set_function(qr, i, 6, (i & 1) == 0);
    }

    static const int k_finders[3][2] = { { 3, 3 }, { -4, 3 }, { 3, -4 } };

    for (int f = 0; f < 3; f++)
    {
        int cx = k_finders[f][0] < 0 ? size + k_finders[f][0] : k_finders[f][0];
        int cy = k_finders[f][1] < 0 ? size + k_finders[f][1] : k_finders[f][1];

        for (int dy = -4; dy <= 4; dy++)
        {
            for (int dx = -4; dx <= 4; dx++)
            {
                int x = cx + dx;
                int y = cy + dy;
                int ax = dx < 0 ? -dx : dx;
                int ay = dy < 0 ? -dy : dy;
                int distance = ax > ay ? ax : ay;

                if (x >= 0 && x < size && y >= 0 && y < size)
                    set_function(qr, x, y, distance != 2 && distance != 4);
            }
        }
    }

    for (int i = 0; i < count; i++)
    {
        for (int j = 0; j < count; j++)
        {
            if ((i == 0 && j == 0) || (i == 0 && j == count - 1) || (i == count - 1 && j == 0))
                continue;

            for (int dy = -2; dy <= 2; dy++)
            {
                for (int dx = -2; dx <= 2; dx++)
                {
                    int ax = dx < 0 ? -dx : dx;
                    int ay = dy < 0 ? -dy : dy;
                    set_function(qr, positions[i] + dx, positions[j] + dy, (ax > ay ? ax : ay) != 1);
                }
            }
        }
    }

    draw_format(qr, 0);

    if (qr->version >= 7)
    {
        int remainder = qr->version;

        for (int i = 0; i < 12; i++)
            remainder = (remainder << 1) ^ ((remainder >> 11) * 0x1F25);

        int bits = (qr->version << 12) | remainder;

        for (int i = 0; i < 18; i++)
        {
            int dark = (bits >> i) & 1;
            int a = size - 11 + i % 3;
            int b = i / 3;
            set_function(qr, a, b, dark);
            set_function(qr, b, a, dark);
        }
    }
}

// Level M has format bits 00
static void draw_format(qr_code_t* qr, int mask)
{
    int size = qr->size;
    int data = mask;
    int remainder = data;

    for (int i = 0; i < 10; i++)
        remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);

    int bits = ((data << 10) | remainder) ^ 0x5412;

    for (int i = 0; i <= 5; i++)
        set_function(qr, 8, i, (bits >> i) & 1);

    set_function(qr, 8, 7, (bits >> 6) & 1);
    set_function(qr, 8, 8, (bits >> 7) & 1);
    set_function(qr, 7, 8, (bits >> 8) & 1);

    for (int i = 9; i < 15; i++)
        set_function(qr, 14 - i, 8, (bits >> i) & 1);

    for (int i = 0; i < 8; i++)
        set_function(qr, size - 1 - i, 8, (bits >> i) & 1);

    for (int i = 8; i < 15; i++)
        set_function(qr, 8, size - 15 + i, (bits >> i) & 1);

    set_function(qr, 8, size - 8, 1);
}

static void draw_codewords(qr_code_t* qr, int length)
{
    int size = qr->size;
    int i = 0;

    for (int right = size - 1; right >= 1; right -= 2)
    {
        if (right == 6)
            right = 5;

        for (int vertical = 0; vertical < size; vertical++)
        {
            for (int j = 0; j < 2; j++)
            {
                int x = right - j;
                int upward = ((right + 1) & 2) == 0;
                int y = upward ? size - 1 - vertical : vertical;

                if ((qr->modules[y][x] & FUNCTION) == 0 && i < length * 8)
                {
                    qr->modules[y][x] = (uint8_t)((s_codewords[i >> 3] >> (7 - (i & 7))) & 1);
                    i++;
                }
            }
        }
    }
}

// One loop per mask and tables instead of divisions, which cost about 40 clocks each on a 386
#define MASK_LOOP(condition) \
    for (int y = 0; y < size; y++) \
    { \
        uint8_t* row = qr->modules[y]; \
        int y3 = s_mod3[y]; \
        (void)y3; \
        for (int x = 0; x < size; x++) \
        { \
            if ((condition) && (row[x] & FUNCTION) == 0) \
                row[x] ^= DARK; \
        } \
    }

static uint8_t s_mod3[QR_MAX_SIZE * QR_MAX_SIZE];
static uint8_t s_div3[QR_MAX_SIZE];

static void apply_mask(qr_code_t* qr, int mask)
{
    int size = qr->size;

    if (s_mod3[1] == 0)
    {
        for (int i = 0; i < QR_MAX_SIZE * QR_MAX_SIZE; i++)
            s_mod3[i] = (uint8_t)(i % 3);

        for (int i = 0; i < QR_MAX_SIZE; i++)
            s_div3[i] = (uint8_t)(i / 3);
    }

    switch (mask)
    {
        case 0: MASK_LOOP(((x + y) & 1) == 0) break;
        case 1: MASK_LOOP((y & 1) == 0) break;
        case 2: MASK_LOOP(s_mod3[x] == 0) break;
        case 3: MASK_LOOP(s_mod3[x + y] == 0) break;
        case 4: MASK_LOOP(((s_div3[x] + (y >> 1)) & 1) == 0) break;
        case 5: MASK_LOOP((x & y & 1) + s_mod3[x * y] == 0) break;
        case 6: MASK_LOOP((((x & y & 1) + s_mod3[x * y]) & 1) == 0) break;
        default: MASK_LOOP(((((x + y) & 1) + s_mod3[x * y]) & 1) == 0) break;
    }
}

#undef MASK_LOOP

static int alignment_positions(int version, int* positions)
{
    if (version == 1)
        return 0;

    int count = version / 7 + 2;
    int step = (version * 4 + count * 2 + 1) / (count * 2 - 2) * 2;
    positions[0] = 6;

    for (int i = count - 1, position = version * 4 + 10; i >= 1; i--, position -= step)
        positions[i] = position;

    return count;
}
