#include "cdc.h"
#include "timebase.h"
#include "towns.h"

#define CDC_MASTER 0x04C0
#define CDC_COMMAND 0x04C2
#define CDC_PARAMETER 0x04C4
#define CDC_TRANSFER 0x04C6
#define CDC_MODE1_READ 0x22
#define READ_TIMEOUT 3000000
#define DMA_TIMEOUT 200000
#define DONE_TIMEOUT 200000

static uint8_t bcd(uint32_t value);
static int wait_packet(cdc_read_t* read, uint8_t* packet, uint32_t timeout);

// Byte transfers, I/O to memory, single mode with auto-init: the ROM's channel 3 mode is 54h
void cdc_dma_setup(int channel, uint32_t address, uint16_t count, uint8_t mode)
{
    out8(0x00A0, 0x03);
    out8(0x00A1, (uint8_t)channel);
    out8(0x00A2, (uint8_t)count);
    out8(0x00A3, (uint8_t)(count >> 8));
    out8(0x00A4, (uint8_t)address);
    out8(0x00A5, (uint8_t)(address >> 8));
    out8(0x00A6, (uint8_t)(address >> 16));
    out8(0x00A7, (uint8_t)(address >> 24));
    out8(0x00A8, 0x20);
    out8(0x00A9, 0x00);
    out8(0x00AA, mode);
    out8(0x00AF, (uint8_t)(0x0F & ~(1 << channel)));
}

void cdc_read_dma(uint32_t lba, uint32_t address, cdc_read_t* read)
{
    uint32_t frames = lba + 150;
    uint8_t packet[4];
    uint8_t parameters[8] = { bcd(frames / 4500), bcd((frames / 75) % 60), bcd(frames % 75) };
    uint32_t start = tb_now();

    parameters[3] = parameters[0];
    parameters[4] = parameters[1];
    parameters[5] = parameters[2];
    read->result = CDC_TIMEOUT;
    read->status_count = 0;

    out8(CDC_MASTER, 0xC0);
    cdc_dma_setup(3, address, 2047, 0x54);

    for (int i = 0; i < 8; i++)
        out8(CDC_PARAMETER, parameters[i]);

    out8(CDC_COMMAND, CDC_MODE1_READ);

    for (;;)
    {
        if (!wait_packet(read, packet, READ_TIMEOUT))
            break;

        if (packet[0] == 0x22)
        {
            out8(CDC_TRANSFER, 0x10);
            uint32_t transfer = tb_now();

            while ((in8(CDC_MASTER) & 0x10) != 0)
            {
                if (tb_now() - transfer > DMA_TIMEOUT)
                    break;
            }

            read->result = (in8(CDC_MASTER) & 0x10) != 0 ? CDC_TIMEOUT : CDC_OK;
            wait_packet(read, packet, DONE_TIMEOUT);
            break;
        }

        if (packet[0] == 0x21)
        {
            read->result = CDC_ERROR;
            break;
        }
    }

    out8(0x00AF, 0x0F);
    out8(CDC_MASTER, 0xC0);
    read->microseconds = tb_now() - start;
}

static uint8_t bcd(uint32_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static int wait_packet(cdc_read_t* read, uint8_t* packet, uint32_t timeout)
{
    uint32_t start = tb_now();

    while ((in8(CDC_MASTER) & 0x02) == 0)
    {
        if (tb_now() - start > timeout)
            return 0;
    }

    for (int i = 0; i < 4; i++)
    {
        packet[i] = in8(CDC_COMMAND);

        if (read->status_count < sizeof(read->status))
            read->status[read->status_count++] = packet[i];
    }

    return 1;
}
