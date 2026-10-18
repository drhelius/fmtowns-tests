#include "record.h"
#include "towns.h"

static uint32_t s_table[256] SHELL_STATE;

void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t value = i;

        for (int bit = 0; bit < 8; bit++)
            value = (value & 1) != 0 ? (value >> 1) ^ 0xEDB88320u : value >> 1;

        s_table[i] = value;
    }
}

// Raw update without the initial and final inversions
uint32_t crc32_update(uint32_t crc, const void* data, uint32_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;

    for (uint32_t i = 0; i < size; i++)
        crc = s_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);

    return crc;
}

uint32_t crc32(const void* data, uint32_t size)
{
    return ~crc32_update(0xFFFFFFFFu, data, size);
}

void record_seal(record_header_t* header)
{
    header->magic = RECORD_MAGIC;
    header->crc = 0;
    uint32_t crc = crc32_update(0xFFFFFFFFu, header, sizeof(*header));
    header->crc = ~crc32_update(crc, header + 1, header->length);
}

int record_valid(const record_header_t* header)
{
    record_header_t copy = *header;

    if (copy.magic != RECORD_MAGIC)
        return 0;

    copy.crc = 0;
    uint32_t crc = crc32_update(0xFFFFFFFFu, &copy, sizeof(copy));
    return ~crc32_update(crc, header + 1, header->length) == header->crc;
}

// RFC 4648 without padding, all within the QR alphanumeric set
int base32_encode(char* output, const uint8_t* data, int size)
{
    static const char k_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    uint32_t buffer = 0;
    int bits = 0;
    int length = 0;

    for (int i = 0; i < size; i++)
    {
        buffer = (buffer << 8) | data[i];
        bits += 8;

        while (bits >= 5)
        {
            output[length++] = k_alphabet[(buffer >> (bits - 5)) & 0x1F];
            bits -= 5;
        }
    }

    if (bits > 0)
        output[length++] = k_alphabet[(buffer << (5 - bits)) & 0x1F];

    output[length] = 0;
    return length;
}
