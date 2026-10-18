#ifndef RECORD_H
#define RECORD_H

#include <stdint.h>

// A test's result record: this header, the test's own payload, sealed with a CRC-32 of both
// The emulator runners find records in RAM by the magic and check the CRC

#define RECORD_MAGIC 0x52544D46u

#define RECORD_STATUS_NONE 0
#define RECORD_STATUS_OK 1
#define RECORD_STATUS_FAULT 2
#define RECORD_STATUS_TIMEOUT 3
#define RECORD_STATUS_SKIP 4

typedef struct
{
    uint32_t magic;
    uint8_t disc;
    uint8_t test;
    uint8_t version;
    uint8_t status;
    uint16_t machine_id;
    uint8_t time_base;
    uint8_t leak;
    uint32_t build;
    uint16_t run;
    uint16_t length;
    uint32_t steps;
    uint32_t crc;
} record_header_t;

void crc32_init(void);
uint32_t crc32_update(uint32_t crc, const void* data, uint32_t size);
uint32_t crc32(const void* data, uint32_t size);
void record_seal(record_header_t* header);
int record_valid(const record_header_t* header);
int base32_encode(char* output, const uint8_t* data, int size);

#endif /* RECORD_H */
