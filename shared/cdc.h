#ifndef CDC_H
#define CDC_H

#include <stdint.h>

// One Mode 1 sector of the disc in the drive, read without the BIOS: the CDC command and its status packets,
// then a DMA transfer on channel 3 as the system ROM programs it

#define CDC_OK 0
#define CDC_TIMEOUT 1
#define CDC_ERROR 2

typedef struct
{
    uint8_t result;
    uint8_t status_count;
    uint8_t status[22];
    uint32_t microseconds;
} cdc_read_t;

void cdc_read_dma(uint32_t lba, uint32_t address, cdc_read_t* read);
void cdc_dma_setup(int channel, uint32_t address, uint16_t count, uint8_t mode);

#endif /* CDC_H */
